/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_HOST_EXECUTOR_HPP
#define WIRELINK_HOST_EXECUTOR_HPP

#include "wirelink/wirelink.h"
#include "wirelink/outbox.h"
#include "wirelink/pump.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <semaphore>
#include <thread>

namespace wirelink::host {

struct ExecutorHooks {
    using ServiceFn = int (*)(void* s_user_data) noexcept;
    using QuiesceFn = void (*)(void* s_user_data) noexcept;
    // Runs on the owner thread after core events have been dispatched. Return
    // true only when another immediate bounded application pass is required.
    using ApplicationProgressFn = bool (*)(void* s_user_data,
                                            wl_ctx_t& s_context,
                                            wl_time_ms_t s_now_ms) noexcept;
    // Side-effect-free relative deadline query. Zero means due now and
    // WL_POLL_NO_DEADLINE_MS disables timed wakeups for this source.
    using DeadlineHintFn = std::uint32_t (*)(const void* s_user_data,
                                             wl_time_ms_t s_now_ms) noexcept;
    // Terminal event dispatcher. For RX, the hook must call wl_event_release()
    // exactly once before returning. For TX terminal events, the hook observes
    // the event before the executor calls wl_tx_take(). Generated WLC runtime
    // dispatchers naturally satisfy this contract. With no hook installed, the
    // executor releases RX events itself.
    using EventFn = void (*)(void* s_user_data, wl_ctx_t& s_context,
                             const wl_event_t& s_event) noexcept;
    void* m_user_data{};
    // Owner-pass order is adapter service, core event dispatch, application
    // progress, and queued TX dispatch. Deadline hints are queried only after a
    // pass reports no immediate work; the executor sleeps until the earliest of
    // core, application, and adapter deadlines. All-none means an unbounded
    // event-driven sleep.
    ServiceFn m_service{};
    QuiesceFn m_quiesce{};
    ApplicationProgressFn m_application_progress{};
    DeadlineHintFn m_application_deadline_hint{};
    DeadlineHintFn m_adapter_deadline_hint{};
    EventFn m_on_event{};
};

struct ExecutorStats {
    std::uint64_t m_feed_calls{};
    std::uint64_t m_feed_bytes{};
    std::uint64_t m_feed_backpressure{};
    std::uint64_t m_rx_events{};
    std::uint64_t m_poll_errors{};
    std::uint64_t m_service_errors{};
    std::uint64_t m_latest_submitted{};
    std::uint64_t m_latest_coalesced{};
    std::uint64_t m_latest_queue_full{};
    std::uint64_t m_latest_dispatched{};
    std::uint64_t m_latest_failed{};
    std::uint64_t m_latest_cancelled{};
};

class Executor {
public:
    static constexpr std::size_t s_kMaximumCommandPayload = 512;
    static constexpr std::size_t s_kLatestLaneCapacity = 8;

    enum class State : std::uint8_t {
        kUninitialized,
        kReady,
        kRunning,
        kStopping,
        kStopped,
    };

    Executor() = default;
    ~Executor();

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
    Executor(Executor&&) = delete;
    Executor& operator=(Executor&&) = delete;

    // The caller owns every buffer in storage until this executor is destroyed.
    // initialize(), setHooks(), setSink(), and context() are setup-only APIs and
    // must be used before start(). Runtime Wirelink access belongs to the owner
    // thread; transports feed RX through feedBytes() and signal deferred adapter
    // work through notify().
    int initialize(const wl_config_t& s_config, const wl_storage_t& s_storage);
    int setHooks(const ExecutorHooks& s_hooks);
    int setSink(wl_sink_fn s_sink, void* s_user_data);
    wl_ctx_t& context() noexcept { return m_context; }

    int start();
    void requestStop() noexcept;
    void stop() noexcept;

    // SPSC producer entry point. No parsing, dispatch, ACK, or user callback is
    // executed on the caller's thread.
    int feedBytes(const std::uint8_t* s_data, std::size_t s_size,
                  std::size_t& s_accepted) noexcept;

    // Wake the owner after adapter-side async completion or direct RX commit.
    void notify() noexcept;

    // Replaces an unsent real-time command in the same message-id lane. Distinct
    // IDs retain independent newest values up to s_kLatestLaneCapacity; a new ID
    // beyond that bound returns WL_ERR_QUEUE_FULL.
    int submitLatest(std::uint16_t s_message_id, const std::uint8_t* s_payload,
                     std::size_t s_payload_size) noexcept;

    State state() const noexcept { return m_state.load(std::memory_order_acquire); }
    ExecutorStats stats() const noexcept;

private:
    struct AtomicStats {
        std::atomic<std::uint64_t> m_feed_calls{};
        std::atomic<std::uint64_t> m_feed_bytes{};
        std::atomic<std::uint64_t> m_feed_backpressure{};
        std::atomic<std::uint64_t> m_rx_events{};
        std::atomic<std::uint64_t> m_poll_errors{};
        std::atomic<std::uint64_t> m_service_errors{};
        std::atomic<std::uint64_t> m_latest_submitted{};
        std::atomic<std::uint64_t> m_latest_coalesced{};
        std::atomic<std::uint64_t> m_latest_queue_full{};
        std::atomic<std::uint64_t> m_latest_dispatched{};
        std::atomic<std::uint64_t> m_latest_failed{};
        std::atomic<std::uint64_t> m_latest_cancelled{};
    };

    static wl_time_ms_t s_nowMs() noexcept;
    static int s_serviceBridge(void* s_user_data) noexcept;
    static void s_quiesceBridge(void* s_user_data) noexcept;
    static std::uint8_t s_applicationProgressBridge(
        void* s_user_data, wl_ctx_t* s_context,
        wl_time_ms_t s_now_ms) noexcept;
    static std::uint32_t s_applicationDeadlineBridge(
        const void* s_user_data, wl_time_ms_t s_now_ms) noexcept;
    static std::uint32_t s_adapterDeadlineBridge(
        const void* s_user_data, wl_time_ms_t s_now_ms) noexcept;
    static void s_eventBridge(void* s_user_data, wl_ctx_t* s_context,
                              const wl_event_t* s_event) noexcept;
    wl_pump_hooks_t s_pumpHooks() noexcept;
    void s_run() noexcept;
    bool s_dispatchOne() noexcept;
    void s_shutdownOnOwner() noexcept;
    wl_ctx_t m_context{};
    ExecutorHooks m_hooks{};
    std::atomic<State> m_state{State::kUninitialized};
    std::atomic<bool> m_accepting{false};
    std::atomic<bool> m_stop_requested{false};
    std::thread m_thread;

    std::mutex m_command_mutex;
    wl_outbox_t m_outbox{};
    std::array<wl_outbox_slot_t, s_kLatestLaneCapacity> m_outbox_slots{};
    std::array<std::uint8_t,
               s_kLatestLaneCapacity * s_kMaximumCommandPayload>
        m_outbox_payloads{};

    std::atomic<std::uint64_t> m_wake_generation{};
    std::counting_semaphore<> m_wake{0};
    std::atomic<std::uint32_t> m_producers_in_flight{};
    AtomicStats m_stats{};
};

} // namespace wirelink::host

#endif // WIRELINK_HOST_EXECUTOR_HPP
