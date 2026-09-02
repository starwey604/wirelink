/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/host/executor.hpp"

#include <chrono>
#include <system_error>

namespace wirelink::host {

namespace {
constexpr std::size_t s_kPollBudget = 64;
} // namespace

Executor::~Executor() {
    stop();
}

int Executor::initialize(const wl_config_t& s_config,
                                 const wl_storage_t& s_storage) {
    if (state() != State::kUninitialized) return WL_ERR_INVALID_STATE;
    if (s_config.max_payload_len > s_kMaximumCommandPayload) {
        return WL_ERR_INVALID_ARG;
    }

    const wl_outbox_config_t s_outbox_config{
        .slots = m_outbox_slots.data(),
        .slot_count = static_cast<std::uint16_t>(m_outbox_slots.size()),
        .payload_storage = m_outbox_payloads.data(),
        .payload_storage_size = m_outbox_payloads.size(),
        .payload_capacity_per_slot = s_kMaximumCommandPayload,
        .initial_generation = 0,
    };
    int s_result = wl_outbox_init(&m_outbox, &s_outbox_config);
    if (s_result != WL_OK) return s_result;
    s_result = wl_init(&m_context, &s_config, &s_storage);
    if (s_result != WL_OK) return s_result;
    m_state.store(State::kReady, std::memory_order_release);
    return WL_OK;
}

int Executor::setHooks(const ExecutorHooks& s_hooks) {
    if (state() != State::kReady) return WL_ERR_INVALID_STATE;
    m_hooks = s_hooks;
    return WL_OK;
}

int Executor::setSink(wl_sink_fn s_sink, void* s_user_data) {
    if (state() != State::kReady) return WL_ERR_INVALID_STATE;
    return wl_set_sink(&m_context, s_sink, s_user_data);
}

int Executor::start() {
    State s_expected = State::kReady;
    if (!m_state.compare_exchange_strong(s_expected, State::kRunning,
                                         std::memory_order_acq_rel)) {
        return WL_ERR_INVALID_STATE;
    }

    m_stop_requested.store(false, std::memory_order_release);
    m_accepting.store(true, std::memory_order_release);
    try {
        m_thread = std::thread([this] { s_run(); });
    } catch (const std::system_error&) {
        m_accepting.store(false, std::memory_order_release);
        m_state.store(State::kReady, std::memory_order_release);
        return WL_ERR_IO;
    }
    notify();
    return WL_OK;
}

void Executor::requestStop() noexcept {
    m_accepting.store(false, std::memory_order_release);
    m_stop_requested.store(true, std::memory_order_release);

    State s_expected = State::kRunning;
    (void)m_state.compare_exchange_strong(s_expected, State::kStopping,
                                          std::memory_order_acq_rel);
    notify();
}

void Executor::stop() noexcept {
    const State s_current = state();
    if (s_current == State::kUninitialized) return;
    if (s_current == State::kStopped) {
        if (m_thread.joinable() &&
            m_thread.get_id() != std::this_thread::get_id()) {
            m_thread.join();
        }
        return;
    }

    if (s_current == State::kReady) {
        m_accepting.store(false, std::memory_order_release);
        auto s_hooks = s_pumpHooks();
        wl_pump_quiesce(&s_hooks);
        (void)wl_set_sink(&m_context, nullptr, nullptr);
        m_state.store(State::kStopped, std::memory_order_release);
        return;
    }

    requestStop();
    if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id()) {
        m_thread.join();
    }
}

int Executor::feedBytes(const std::uint8_t* s_data,
                                std::size_t s_size,
                                std::size_t& s_accepted) noexcept {
    s_accepted = 0;
    if (!m_accepting.load(std::memory_order_acquire)) {
        return WL_ERR_CANCELLED;
    }

    m_producers_in_flight.fetch_add(1, std::memory_order_acq_rel);
    if (!m_accepting.load(std::memory_order_acquire)) {
        if (m_producers_in_flight.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            m_producers_in_flight.notify_all();
        }
        return WL_ERR_CANCELLED;
    }

    m_stats.m_feed_calls.fetch_add(1, std::memory_order_relaxed);
    const int s_result = wl_feed_bytes(&m_context, s_data, s_size, &s_accepted);
    m_stats.m_feed_bytes.fetch_add(s_accepted, std::memory_order_relaxed);
    if (s_result == WL_ERR_WOULD_BLOCK || s_result == WL_ERR_NO_SPACE) {
        m_stats.m_feed_backpressure.fetch_add(1, std::memory_order_relaxed);
    }

    if (m_producers_in_flight.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        m_producers_in_flight.notify_all();
    }
    notify();
    return s_result;
}

void Executor::notify() noexcept {
    m_wake_generation.fetch_add(1, std::memory_order_release);
    m_wake.release();
}

int Executor::submitLatest(std::uint16_t s_message_id,
                                   const std::uint8_t* s_payload,
                                   std::size_t s_payload_size) noexcept {
    if (s_message_id == 0 || s_payload_size > s_kMaximumCommandPayload ||
        (s_payload == nullptr && s_payload_size != 0)) {
        return WL_ERR_INVALID_ARG;
    }
    if (!m_accepting.load(std::memory_order_acquire)) {
        return WL_ERR_CANCELLED;
    }

    {
        std::lock_guard<std::mutex> s_lock(m_command_mutex);
        if (!m_accepting.load(std::memory_order_relaxed)) {
            return WL_ERR_CANCELLED;
        }
        std::uint8_t s_coalesced{};
        const int s_result = wl_outbox_submit_latest(
            &m_outbox, s_message_id, s_payload, s_payload_size,
            &s_coalesced);
        if (s_result == WL_ERR_QUEUE_FULL) {
            m_stats.m_latest_queue_full.fetch_add(1,
                                                  std::memory_order_relaxed);
            return s_result;
        }
        if (s_result != WL_OK) return s_result;
        if (s_coalesced != 0U) {
            m_stats.m_latest_coalesced.fetch_add(
                1, std::memory_order_relaxed);
        }
    }

    m_stats.m_latest_submitted.fetch_add(1, std::memory_order_relaxed);
    notify();
    return WL_OK;
}

wl_time_ms_t Executor::s_nowMs() noexcept {
    using namespace std::chrono;
    return static_cast<wl_time_ms_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

int Executor::s_serviceBridge(void* s_user_data) noexcept {
    auto& s_self = *static_cast<Executor*>(s_user_data);
    return s_self.m_hooks.m_service(s_self.m_hooks.m_user_data);
}

void Executor::s_quiesceBridge(void* s_user_data) noexcept {
    auto& s_self = *static_cast<Executor*>(s_user_data);
    s_self.m_hooks.m_quiesce(s_self.m_hooks.m_user_data);
}

std::uint8_t Executor::s_applicationProgressBridge(
    void* s_user_data, wl_ctx_t* s_context,
    wl_time_ms_t s_now_ms) noexcept {
    auto& s_self = *static_cast<Executor*>(s_user_data);
    return s_self.m_hooks.m_application_progress(
               s_self.m_hooks.m_user_data, *s_context, s_now_ms)
               ? 1U
               : 0U;
}

std::uint32_t Executor::s_applicationDeadlineBridge(
    const void* s_user_data, wl_time_ms_t s_now_ms) noexcept {
    const auto& s_self = *static_cast<const Executor*>(s_user_data);
    return s_self.m_hooks.m_application_deadline_hint(
        s_self.m_hooks.m_user_data, s_now_ms);
}

std::uint32_t Executor::s_adapterDeadlineBridge(
    const void* s_user_data, wl_time_ms_t s_now_ms) noexcept {
    const auto& s_self = *static_cast<const Executor*>(s_user_data);
    return s_self.m_hooks.m_adapter_deadline_hint(
        s_self.m_hooks.m_user_data, s_now_ms);
}

void Executor::s_eventBridge(void* s_user_data, wl_ctx_t* s_context,
                             const wl_event_t* s_event) noexcept {
    auto& s_self = *static_cast<Executor*>(s_user_data);
    s_self.m_hooks.m_on_event(s_self.m_hooks.m_user_data, *s_context,
                              *s_event);
}

wl_pump_hooks_t Executor::s_pumpHooks() noexcept {
    return wl_pump_hooks_t{
        .user_data = this,
        .service = m_hooks.m_service != nullptr ? &Executor::s_serviceBridge
                                                : nullptr,
        .quiesce = m_hooks.m_quiesce != nullptr ? &Executor::s_quiesceBridge
                                                : nullptr,
        .application_progress =
            m_hooks.m_application_progress != nullptr
                ? &Executor::s_applicationProgressBridge
                : nullptr,
        .application_deadline_hint =
            m_hooks.m_application_deadline_hint != nullptr
                ? &Executor::s_applicationDeadlineBridge
                : nullptr,
        .adapter_deadline_hint =
            m_hooks.m_adapter_deadline_hint != nullptr
                ? &Executor::s_adapterDeadlineBridge
                : nullptr,
        .on_event = m_hooks.m_on_event != nullptr ? &Executor::s_eventBridge
                                                  : nullptr,
    };
}

void Executor::s_run() noexcept {
    auto s_pump_hooks = s_pumpHooks();
    while (!m_stop_requested.load(std::memory_order_acquire)) {
        for (std::size_t s_index = 0;
             s_index < s_kPollBudget && m_wake.try_acquire(); ++s_index) {
        }
        const std::uint64_t s_observed_wake =
            m_wake_generation.load(std::memory_order_acquire);
        wl_pump_result_t s_pump_result{};
        const int s_step_result = wl_pump_step(
            &m_context, s_nowMs(), s_kPollBudget, &s_pump_hooks,
            &s_pump_result);
        if (s_step_result != WL_OK) {
            m_stats.m_poll_errors.fetch_add(1, std::memory_order_relaxed);
        }
        m_stats.m_rx_events.fetch_add(s_pump_result.rx_events,
                                      std::memory_order_relaxed);
        m_stats.m_poll_errors.fetch_add(s_pump_result.poll_errors,
                                        std::memory_order_relaxed);
        m_stats.m_service_errors.fetch_add(s_pump_result.service_errors,
                                           std::memory_order_relaxed);
        bool s_progress = s_pump_result.progress != 0U;
        if (m_stop_requested.load(std::memory_order_acquire)) break;

        if (s_dispatchOne()) {
            s_progress = true;
        }
        if (s_progress) continue;

        wl_poll_hint_t s_hint{};
        const wl_time_ms_t s_hint_now = s_nowMs();
        const int s_hint_result = wl_pump_get_hint(
            &m_context, s_hint_now, &s_pump_hooks, &s_hint);
        if (s_hint_result != WL_OK) {
            m_stats.m_poll_errors.fetch_add(1, std::memory_order_relaxed);
        } else if (s_hint.work_pending != 0) {
            continue;
        }

        std::uint32_t s_next_deadline =
            s_hint_result == WL_OK ? s_hint.next_deadline_ms
                                   : WL_POLL_NO_DEADLINE_MS;
        if (s_next_deadline == 0) continue;

        if (m_stop_requested.load(std::memory_order_acquire) ||
            m_wake_generation.load(std::memory_order_acquire) !=
                s_observed_wake) {
            continue;
        }
        if (s_next_deadline != WL_POLL_NO_DEADLINE_MS) {
            (void)m_wake.try_acquire_for(
                std::chrono::milliseconds(s_next_deadline));
        } else {
            m_wake.acquire();
        }
    }

    s_shutdownOnOwner();
    m_state.store(State::kStopped, std::memory_order_release);
}

bool Executor::s_dispatchOne() noexcept {
    std::array<std::uint8_t, s_kMaximumCommandPayload> s_payload{};
    wl_outbox_item_t s_item{};
    {
        std::lock_guard<std::mutex> s_lock(m_command_mutex);
        const int s_acquired = wl_outbox_acquire_copy(
            &m_outbox, s_payload.data(), s_payload.size(), &s_item);
        if (s_acquired == WL_ERR_NO_DATA) return false;
        if (s_acquired != WL_OK) {
            m_stats.m_latest_failed.fetch_add(1,
                                               std::memory_order_relaxed);
            return false;
        }
    }

    const int s_result = wl_send_unreliable(
        &m_context, s_item.message_id, s_payload.data(),
        s_item.payload_length);
    const bool s_deferred = s_result == WL_ERR_BUSY ||
                            s_result == WL_ERR_WOULD_BLOCK ||
                            s_result == WL_ERR_NO_SPACE;
    {
        std::lock_guard<std::mutex> s_lock(m_command_mutex);
        (void)wl_outbox_complete(
            &m_outbox, &s_item,
            s_result == WL_OK
                ? WL_OUTBOX_ACCEPTED
                : (s_deferred ? WL_OUTBOX_DEFERRED
                              : WL_OUTBOX_REJECTED));
    }
    if (s_result == WL_OK) {
        m_stats.m_latest_dispatched.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    if (s_deferred) {
        return false;
    }

    m_stats.m_latest_failed.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void Executor::s_shutdownOnOwner() noexcept {
    m_accepting.store(false, std::memory_order_release);
    auto s_pump_hooks = s_pumpHooks();
    wl_pump_quiesce(&s_pump_hooks);

    std::uint32_t s_producers =
        m_producers_in_flight.load(std::memory_order_acquire);
    while (s_producers != 0) {
        m_producers_in_flight.wait(s_producers, std::memory_order_acquire);
        s_producers = m_producers_in_flight.load(std::memory_order_acquire);
    }

    if (s_pump_hooks.service != nullptr) {
        const int s_service_result = s_pump_hooks.service(this);
        if (s_service_result != WL_OK && s_service_result != WL_ERR_NO_DATA &&
            s_service_result != WL_ERR_WOULD_BLOCK) {
            m_stats.m_service_errors.fetch_add(1,
                                               std::memory_order_relaxed);
        }
    }

    {
        std::lock_guard<std::mutex> s_lock(m_command_mutex);
        std::uint16_t s_cancelled{};
        (void)wl_outbox_reset(&m_outbox, &s_cancelled);
        if (s_cancelled != 0) {
            m_stats.m_latest_cancelled.fetch_add(s_cancelled,
                                                 std::memory_order_relaxed);
        }
    }
    (void)wl_set_sink(&m_context, nullptr, nullptr);
}

ExecutorStats Executor::stats() const noexcept {
    return ExecutorStats{
        .m_feed_calls = m_stats.m_feed_calls.load(std::memory_order_relaxed),
        .m_feed_bytes = m_stats.m_feed_bytes.load(std::memory_order_relaxed),
        .m_feed_backpressure =
            m_stats.m_feed_backpressure.load(std::memory_order_relaxed),
        .m_rx_events = m_stats.m_rx_events.load(std::memory_order_relaxed),
        .m_poll_errors = m_stats.m_poll_errors.load(std::memory_order_relaxed),
        .m_service_errors =
            m_stats.m_service_errors.load(std::memory_order_relaxed),
        .m_latest_submitted =
            m_stats.m_latest_submitted.load(std::memory_order_relaxed),
        .m_latest_coalesced =
            m_stats.m_latest_coalesced.load(std::memory_order_relaxed),
        .m_latest_queue_full =
            m_stats.m_latest_queue_full.load(std::memory_order_relaxed),
        .m_latest_dispatched =
            m_stats.m_latest_dispatched.load(std::memory_order_relaxed),
        .m_latest_failed =
            m_stats.m_latest_failed.load(std::memory_order_relaxed),
        .m_latest_cancelled =
            m_stats.m_latest_cancelled.load(std::memory_order_relaxed),
    };
}

} // namespace wirelink::host
