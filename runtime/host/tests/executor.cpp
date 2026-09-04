/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/host/executor.hpp"

#include "wirelink/frame.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using WirelinkExecutor = wirelink::host::Executor;
using WirelinkExecutorHooks = wirelink::host::ExecutorHooks;

void require(bool s_condition, const char* s_message) {
    if (!s_condition) throw std::runtime_error(s_message);
}

template <typename Predicate>
void waitFor(std::condition_variable& s_cv, std::mutex& s_mutex,
             Predicate s_predicate, const char* s_message) {
    std::unique_lock<std::mutex> s_lock(s_mutex);
    require(s_cv.wait_for(s_lock, std::chrono::seconds(5), s_predicate),
            s_message);
}

struct LinkStorage {
    std::array<std::uint8_t, 128> tx_payload{};
    std::array<std::uint8_t, 256> tx_unit{};
    std::array<std::uint8_t, 128> control_unit{};
    std::array<std::uint8_t, 1024> rx_fifo{};
    std::array<std::uint8_t, 256> rx_fallback{};

    wl_storage_t descriptor() {
        return wl_storage_t{
            .tx_payload = tx_payload.data(),
            .tx_payload_size = tx_payload.size(),
            .tx_unit = tx_unit.data(),
            .tx_unit_size = tx_unit.size(),
            .control_unit = control_unit.data(),
            .control_unit_size = control_unit.size(),
            .rx_fifo = rx_fifo.data(),
            .rx_fifo_size = rx_fifo.size(),
            .rx_fallback = rx_fallback.data(),
            .rx_fallback_size = rx_fallback.size(),
        };
    }
};

wl_config_t makeConfig(wl_envelope_type_t s_envelope, std::uint64_t s_session,
                       std::uint32_t s_ack_timeout_ms = 20) {
    return wl_config_t{
        .max_payload_len = 128,
        .envelope = s_envelope,
        .integrity = WL_INTEGRITY_NONE,
        .session_id = s_session,
        .max_retries = 1,
        .ack_timeout_ms = s_ack_timeout_ms,
        .max_transmission_unit = 256,
    };
}

std::vector<std::uint8_t> makeCobsFrame(std::uint16_t s_message_id,
                                        std::uint64_t s_session_id,
                                        const std::uint8_t* s_payload,
                                        std::size_t s_payload_size) {
    const wl_wire_packet_t s_packet{
        .type = WL_PACKET_DATA,
        .integrity = WL_INTEGRITY_NONE,
        .flags = 0,
        .message_id = s_message_id,
        .session_id = s_session_id,
        .sequence = 0,
        .payload = s_payload,
        .payload_len = s_payload_size,
    };
    std::vector<std::uint8_t> s_frame(256);
    std::size_t s_encoded_size{};
    require(wl_frame_encode(&s_packet, WL_ENVELOPE_COBS_STREAM,
                            s_frame.data(), s_frame.size(),
                            &s_encoded_size) == WL_OK,
            "failed to encode COBS test frame");
    s_frame.resize(s_encoded_size);
    return s_frame;
}

struct RxCapture {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::vector<std::uint8_t>> payloads;
    std::thread::id callback_thread{};
    std::thread::id quiesce_thread{};
    std::size_t event_count{};
    std::size_t release_count{};
    bool hold_after_final_release{true};
    bool continue_after_final_release{};
    std::atomic<std::size_t> service_calls{};

    static int service(void* s_user_data) noexcept {
        auto& s_self = *static_cast<RxCapture*>(s_user_data);
        s_self.service_calls.fetch_add(1, std::memory_order_relaxed);
        s_self.cv.notify_all();
        return WL_ERR_NO_DATA;
    }

    static wl_pump_event_disposition_t onEvent(
        void* s_user_data, wl_ctx_t& s_context,
        const wl_event_t& s_event, wl_time_ms_t s_now_ms) noexcept {
        auto& s_self = *static_cast<RxCapture*>(s_user_data);
        (void)s_now_ms;
        {
            std::lock_guard<std::mutex> s_lock(s_self.mutex);
            s_self.callback_thread = std::this_thread::get_id();
            s_self.payloads.emplace_back(
                s_event.payload, s_event.payload + s_event.payload_len);
            ++s_self.event_count;
        }
        wl_event_release(&s_context, &s_event);
        {
            std::unique_lock<std::mutex> s_lock(s_self.mutex);
            ++s_self.release_count;
            s_self.cv.notify_all();
            if (s_self.release_count == 2 &&
                s_self.hold_after_final_release) {
                s_self.cv.wait(s_lock, [&] {
                    return s_self.continue_after_final_release;
                });
            }
        }
        return WL_PUMP_EVENT_CONSUMED;
    }

    static void quiesce(void* s_user_data) noexcept {
        auto& s_self = *static_cast<RxCapture*>(s_user_data);
        std::lock_guard<std::mutex> s_lock(s_self.mutex);
        s_self.quiesce_thread = std::this_thread::get_id();
    }
};

void testFeedOnlyWakesOwner() {
    LinkStorage s_storage;
    WirelinkExecutor s_executor;
    const auto s_config =
        makeConfig(WL_ENVELOPE_COBS_STREAM, UINT64_C(0x1000000000000001));
    const auto s_descriptor = s_storage.descriptor();
    require(s_executor.initialize(s_config, s_descriptor) == WL_OK,
            "executor initialization failed");

    RxCapture s_capture;
    WirelinkExecutorHooks s_hooks{};
    s_hooks.m_user_data = &s_capture;
    s_hooks.m_service = RxCapture::service;
    s_hooks.m_on_event = RxCapture::onEvent;
    s_hooks.m_quiesce = RxCapture::quiesce;
    require(s_executor.setHooks(s_hooks) == WL_OK, "setHooks failed");
    require(s_executor.start() == WL_OK, "executor start failed");

    constexpr std::array<std::uint8_t, 4> s_payload_a{1, 0, 3, 5};
    constexpr std::array<std::uint8_t, 3> s_payload_b{8, 13, 21};
    const auto s_frame_a = makeCobsFrame(
        0x1234, UINT64_C(0x2000000000000002), s_payload_a.data(),
        s_payload_a.size());
    const auto s_frame_b = makeCobsFrame(
        0x1235, UINT64_C(0x2000000000000002), s_payload_b.data(),
        s_payload_b.size());
    std::vector<std::uint8_t> s_frames;
    s_frames.reserve(s_frame_a.size() + s_frame_b.size());
    s_frames.insert(s_frames.end(), s_frame_a.begin(), s_frame_a.end());
    s_frames.insert(s_frames.end(), s_frame_b.begin(), s_frame_b.end());
    std::thread::id s_producer_thread;
    int s_feed_result{WL_ERR_INVALID_STATE};
    std::size_t s_accepted{};
    std::thread s_producer([&] {
        s_producer_thread = std::this_thread::get_id();
        s_feed_result =
            s_executor.feedBytes(s_frames.data(), s_frames.size(), s_accepted);
    });
    s_producer.join();

    {
        std::unique_lock<std::mutex> s_lock(s_capture.mutex);
        const bool s_received = s_capture.cv.wait_for(
            s_lock, std::chrono::seconds(5),
            [&] { return s_capture.release_count == 2; });
        if (!s_received) {
            const auto s_stats = s_executor.stats();
            std::fprintf(stderr,
                         "RX timeout: feed=%d accepted=%zu state=%u calls=%llu "
                         "bytes=%llu events=%llu poll_errors=%llu\n",
                         s_feed_result, s_accepted,
                         static_cast<unsigned>(s_executor.state()),
                         static_cast<unsigned long long>(s_stats.m_feed_calls),
                         static_cast<unsigned long long>(s_stats.m_feed_bytes),
                         static_cast<unsigned long long>(s_stats.m_rx_events),
                         static_cast<unsigned long long>(s_stats.m_poll_errors));
        }
        require(s_received, "RX event was not dispatched");
    }
    // Hold the owner in the final callback while queuing a fully-published
    // wake.  That removes the generation/semaphore publication race from the
    // measurement: the next pass must consume this wake, and no earlier wake
    // can arrive after the sample point.
    const auto s_service_calls_before_barrier =
        s_capture.service_calls.load(std::memory_order_relaxed);
    s_executor.notify();
    {
        std::lock_guard<std::mutex> s_lock(s_capture.mutex);
        s_capture.continue_after_final_release = true;
        s_capture.cv.notify_all();
    }
    waitFor(s_capture.cv, s_capture.mutex,
            [&] {
                return s_capture.service_calls.load(
                           std::memory_order_relaxed) >
                       s_service_calls_before_barrier;
            },
            "executor did not service the idle barrier");

    require(s_feed_result == WL_OK && s_accepted == s_frames.size(),
            "producer did not feed the complete frames");
    require(s_capture.event_count == 2 && s_capture.release_count == 2,
            "terminal event hook did not release each event exactly once");
    require(s_capture.payloads.size() == 2 &&
                s_capture.payloads[0] == std::vector<std::uint8_t>(
                                             s_payload_a.begin(),
                                             s_payload_a.end()) &&
                s_capture.payloads[1] == std::vector<std::uint8_t>(
                                             s_payload_b.begin(),
                                             s_payload_b.end()),
            "RX payloads mismatch");
    require(s_capture.callback_thread != s_producer_thread,
            "RX callback ran on the producer thread");

    const auto s_idle_service_calls =
        s_capture.service_calls.load(std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    require(s_capture.service_calls.load(std::memory_order_relaxed) ==
                s_idle_service_calls,
            "idle executor used a periodic busy poll");

    const auto s_owner_thread = s_capture.callback_thread;
    s_executor.stop();
    require(s_capture.quiesce_thread == s_owner_thread,
            "quiesce did not run on the executor owner thread");
    std::size_t s_after_stop_accepted{99};
    require(s_executor.feedBytes(s_frames.data(), s_frames.size(),
                                 s_after_stop_accepted) == WL_ERR_CANCELLED &&
                s_after_stop_accepted == 0,
            "feed was accepted after shutdown");
}

struct AsyncSink {
    WirelinkExecutor* executor{};
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::vector<std::uint8_t>> payloads;
    std::vector<std::uint16_t> message_ids;
    wl_io_token_t token{};
    bool first_in_flight{};
    bool release_requested{};
    bool hold_first{true};
    std::thread::id sink_thread{};
    std::thread::id service_thread{};

    static wl_sink_result_t sink(void* s_user_data, wl_io_token_t s_token,
                                 const std::uint8_t* s_data,
                                 std::size_t s_size) noexcept {
        auto& s_self = *static_cast<AsyncSink*>(s_user_data);
        wl_frame_view_t s_frame{};
        if (wl_frame_decode(s_data, s_size, WL_INTEGRITY_NONE, &s_frame) !=
            WL_OK) {
            return WL_SINK_FAILED;
        }

        std::lock_guard<std::mutex> s_lock(s_self.mutex);
        s_self.sink_thread = std::this_thread::get_id();
        s_self.message_ids.push_back(s_frame.message_id);
        s_self.payloads.emplace_back(s_frame.payload.data,
                                     s_frame.payload.data +
                                         s_frame.payload.length);
        if (s_self.hold_first && s_self.payloads.size() == 1) {
            s_self.token = s_token;
            s_self.first_in_flight = true;
            s_self.cv.notify_all();
            return WL_SINK_STARTED;
        }
        s_self.cv.notify_all();
        return WL_SINK_SENT;
    }

    static int service(void* s_user_data) noexcept {
        auto& s_self = *static_cast<AsyncSink*>(s_user_data);
        wl_io_token_t s_token{};
        {
            std::lock_guard<std::mutex> s_lock(s_self.mutex);
            s_self.service_thread = std::this_thread::get_id();
            if (!s_self.first_in_flight || !s_self.release_requested) {
                return WL_ERR_NO_DATA;
            }
            s_self.first_in_flight = false;
            s_self.release_requested = false;
            s_token = s_self.token;
        }
        return wl_tx_complete(&s_self.executor->context(), s_token, WL_OK);
    }
};

void testLatestLanesCoalesceWithoutCrossMessageLoss() {
    LinkStorage s_storage;
    WirelinkExecutor s_executor;
    const auto s_config =
        makeConfig(WL_ENVELOPE_NATIVE_PACKET, UINT64_C(0x3000000000000003));
    const auto s_descriptor = s_storage.descriptor();
    require(s_executor.initialize(s_config, s_descriptor) == WL_OK,
            "native executor initialization failed");

    AsyncSink s_sink;
    s_sink.executor = &s_executor;
    WirelinkExecutorHooks s_hooks{};
    s_hooks.m_user_data = &s_sink;
    s_hooks.m_service = AsyncSink::service;
    require(s_executor.setHooks(s_hooks) == WL_OK, "setHooks failed");
    require(s_executor.setSink(AsyncSink::sink, &s_sink) == WL_OK,
            "setSink failed");
    require(s_executor.start() == WL_OK, "executor start failed");

    const std::uint8_t s_first = 0;
    require(s_executor.submitLatest(0x40, &s_first, 1) == WL_OK,
            "initial latest submit failed");
    waitFor(s_sink.cv, s_sink.mutex,
            [&] { return s_sink.payloads.size() == 1; },
            "first latest command was not dispatched");

    for (std::uint16_t s_value = 1; s_value <= 100; ++s_value) {
        const auto s_byte = static_cast<std::uint8_t>(s_value);
        require(s_executor.submitLatest(0x40, &s_byte, 1) == WL_OK,
                "latest replacement failed");
    }
    for (std::uint16_t s_value = 201; s_value <= 250; ++s_value) {
        const auto s_byte = static_cast<std::uint8_t>(s_value);
        require(s_executor.submitLatest(0x41, &s_byte, 1) == WL_OK,
                "second latest lane submit failed");
    }

    {
        std::lock_guard<std::mutex> s_lock(s_sink.mutex);
        s_sink.release_requested = true;
    }
    s_executor.notify();
    waitFor(s_sink.cv, s_sink.mutex,
            [&] { return s_sink.payloads.size() >= 3; },
            "coalesced latest lanes were not dispatched");

    s_executor.stop();
    require(s_sink.payloads.size() == 3,
            "intermediate latest commands escaped coalescing");
    std::size_t s_joint_count{};
    std::size_t s_gripper_count{};
    bool s_saw_newest_joint{};
    bool s_saw_newest_gripper{};
    for (std::size_t s_index = 0; s_index < s_sink.payloads.size(); ++s_index) {
        require(s_sink.payloads[s_index].size() == 1,
                "latest payload size mismatch");
        if (s_sink.message_ids[s_index] == 0x40) {
            ++s_joint_count;
            s_saw_newest_joint |= s_sink.payloads[s_index][0] == 100;
        } else if (s_sink.message_ids[s_index] == 0x41) {
            ++s_gripper_count;
            s_saw_newest_gripper |= s_sink.payloads[s_index][0] == 250;
        }
    }
    require(s_joint_count == 2 && s_gripper_count == 1 &&
                s_saw_newest_joint && s_saw_newest_gripper,
            "message-id keyed lanes lost joint or gripper state");
    require(s_sink.sink_thread == s_sink.service_thread,
            "sink and adapter service had different owners");

    const auto s_stats = s_executor.stats();
    require(s_stats.m_latest_submitted == 151,
            "latest submit counter mismatch");
    require(s_stats.m_latest_dispatched == 3,
            "latest dispatch counter mismatch");
    require(s_stats.m_latest_coalesced >= 148,
            "latest commands were not counted as coalesced");
    require(s_stats.m_latest_queue_full == 0,
            "latest lanes unexpectedly overflowed");
}

struct ShutdownCapture {
    WirelinkExecutor* executor{};
    std::mutex mutex;
    std::condition_variable cv;
    std::size_t sink_calls{};
    wl_io_token_t token{};
    std::thread::id sink_thread{};
    std::thread::id quiesce_thread{};

    static wl_sink_result_t sink(void* s_user_data, wl_io_token_t s_token,
                                 const std::uint8_t*, std::size_t) noexcept {
        auto& s_self = *static_cast<ShutdownCapture*>(s_user_data);
        std::lock_guard<std::mutex> s_lock(s_self.mutex);
        ++s_self.sink_calls;
        s_self.token = s_token;
        s_self.sink_thread = std::this_thread::get_id();
        s_self.cv.notify_all();
        return WL_SINK_STARTED;
    }

    static void quiesce(void* s_user_data) noexcept {
        auto& s_self = *static_cast<ShutdownCapture*>(s_user_data);
        wl_io_token_t s_token{};
        {
            std::lock_guard<std::mutex> s_lock(s_self.mutex);
            s_self.quiesce_thread = std::this_thread::get_id();
            s_token = s_self.token;
            s_self.token = 0;
        }
        if (s_token != 0) {
            (void)wl_tx_complete(&s_self.executor->context(), s_token, WL_OK);
        }
    }
};

void testBoundedLatestAndDeterministicShutdown() {
    LinkStorage s_storage;
    WirelinkExecutor s_executor;
    const auto s_config = makeConfig(WL_ENVELOPE_NATIVE_PACKET,
                                     UINT64_C(0x6000000000000006), 1000);
    const auto s_descriptor = s_storage.descriptor();
    require(s_executor.initialize(s_config, s_descriptor) == WL_OK,
            "shutdown executor initialization failed");

    ShutdownCapture s_capture;
    s_capture.executor = &s_executor;
    WirelinkExecutorHooks s_hooks{};
    s_hooks.m_user_data = &s_capture;
    s_hooks.m_quiesce = ShutdownCapture::quiesce;
    require(s_executor.setHooks(s_hooks) == WL_OK, "shutdown hooks failed");
    require(s_executor.setSink(ShutdownCapture::sink, &s_capture) == WL_OK,
            "shutdown sink failed");
    require(s_executor.start() == WL_OK, "shutdown executor start failed");

    const std::uint8_t s_payload = 0x7f;
    require(s_executor.submitLatest(0x61, &s_payload, 1) == WL_OK,
            "active latest submit failed");
    waitFor(s_capture.cv, s_capture.mutex,
            [&] { return s_capture.sink_calls == 1; },
            "latest command never entered the async sink");
    const auto s_dispatch_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (s_executor.stats().m_latest_dispatched != 1 &&
           std::chrono::steady_clock::now() < s_dispatch_deadline) {
        std::this_thread::yield();
    }
    require(s_executor.stats().m_latest_dispatched == 1,
            "active latest command did not leave its coalescing lane");

    for (std::size_t s_index = 0;
         s_index < WirelinkExecutor::s_kLatestLaneCapacity; ++s_index) {
        require(s_executor.submitLatest(
                    static_cast<std::uint16_t>(0x70 + s_index), &s_payload,
                    1) == WL_OK,
                "latest lane table filled too early");
    }
    require(s_executor.submitLatest(0x7f, &s_payload, 1) == WL_ERR_QUEUE_FULL,
            "latest lane table accepted a distinct overflow message");

    const auto s_before = std::chrono::steady_clock::now();
    s_executor.stop();
    const auto s_elapsed = std::chrono::steady_clock::now() - s_before;
    require(s_elapsed < std::chrono::seconds(1),
            "shutdown waited for the async sink timeout");
    require(s_capture.quiesce_thread == s_capture.sink_thread,
            "shutdown hook did not run on the owner thread");

    const auto s_stats = s_executor.stats();
    require(s_stats.m_latest_queue_full == 1 &&
                s_stats.m_latest_cancelled ==
                    WirelinkExecutor::s_kLatestLaneCapacity,
            "latest lane bounds or shutdown counters mismatch");
}

struct SchedulerCapture {
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::uint32_t application_delay_ms{50};
    std::uint32_t adapter_delay_ms{WL_POLL_NO_DEADLINE_MS};
    wl_time_ms_t started_ms{};
    std::chrono::steady_clock::time_point started_at{};
    std::chrono::steady_clock::time_point application_fired_at{};
    std::chrono::steady_clock::time_point adapter_fired_at{};
    bool started{};
    bool application_fired{};
    bool adapter_fired{};
    std::atomic<std::size_t> progress_calls{};
    std::atomic<std::size_t> service_calls{};

    static std::uint32_t remaining(wl_time_ms_t s_now_ms,
                                   wl_time_ms_t s_started_ms,
                                   std::uint32_t s_delay_ms) noexcept {
        const std::uint32_t s_elapsed = s_now_ms - s_started_ms;
        return s_elapsed >= s_delay_ms ? 0 : s_delay_ms - s_elapsed;
    }

    static bool applicationProgress(void* s_user_data, wl_ctx_t&,
                                    wl_time_ms_t s_now_ms) noexcept {
        auto& s_self = *static_cast<SchedulerCapture*>(s_user_data);
        s_self.progress_calls.fetch_add(1, std::memory_order_relaxed);
        bool s_notify{};
        {
            std::lock_guard<std::mutex> s_lock(s_self.mutex);
            if (!s_self.started) {
                s_self.started = true;
                s_self.started_ms = s_now_ms;
                s_self.started_at = std::chrono::steady_clock::now();
            }
            if (!s_self.application_fired &&
                remaining(s_now_ms, s_self.started_ms,
                          s_self.application_delay_ms) == 0) {
                s_self.application_fired = true;
                s_self.application_fired_at =
                    std::chrono::steady_clock::now();
                s_notify = true;
            }
        }
        if (s_notify) s_self.cv.notify_all();
        return false;
    }

    static std::uint32_t applicationDeadline(
        const void* s_user_data, wl_time_ms_t s_now_ms) noexcept {
        const auto& s_self =
            *static_cast<const SchedulerCapture*>(s_user_data);
        std::lock_guard<std::mutex> s_lock(s_self.mutex);
        if (!s_self.started || s_self.application_fired) {
            return WL_POLL_NO_DEADLINE_MS;
        }
        return remaining(s_now_ms, s_self.started_ms,
                         s_self.application_delay_ms);
    }

    static int service(void* s_user_data) noexcept {
        auto& s_self = *static_cast<SchedulerCapture*>(s_user_data);
        s_self.service_calls.fetch_add(1, std::memory_order_relaxed);
        bool s_notify{};
        {
            std::lock_guard<std::mutex> s_lock(s_self.mutex);
            if (s_self.started && !s_self.adapter_fired &&
                s_self.adapter_delay_ms != WL_POLL_NO_DEADLINE_MS &&
                std::chrono::steady_clock::now() - s_self.started_at >=
                    std::chrono::milliseconds(s_self.adapter_delay_ms)) {
                s_self.adapter_fired = true;
                s_self.adapter_fired_at = std::chrono::steady_clock::now();
                s_notify = true;
            }
        }
        if (s_notify) s_self.cv.notify_all();
        return WL_ERR_NO_DATA;
    }

    static std::uint32_t adapterDeadline(
        const void* s_user_data, wl_time_ms_t s_now_ms) noexcept {
        const auto& s_self =
            *static_cast<const SchedulerCapture*>(s_user_data);
        std::lock_guard<std::mutex> s_lock(s_self.mutex);
        if (!s_self.started || s_self.adapter_fired ||
            s_self.adapter_delay_ms == WL_POLL_NO_DEADLINE_MS) {
            return WL_POLL_NO_DEADLINE_MS;
        }
        return remaining(s_now_ms, s_self.started_ms,
                         s_self.adapter_delay_ms);
    }
};

void runApplicationDeadlineTest(SchedulerCapture& s_capture) {
    LinkStorage s_storage;
    WirelinkExecutor s_executor;
    const auto s_config = makeConfig(
        WL_ENVELOPE_NATIVE_PACKET, UINT64_C(0x8000000000000008));
    const auto s_descriptor = s_storage.descriptor();
    require(s_executor.initialize(s_config, s_descriptor) == WL_OK,
            "deadline executor initialization failed");

    WirelinkExecutorHooks s_hooks{};
    s_hooks.m_user_data = &s_capture;
    s_hooks.m_service = SchedulerCapture::service;
    s_hooks.m_application_progress = SchedulerCapture::applicationProgress;
    s_hooks.m_application_deadline_hint =
        SchedulerCapture::applicationDeadline;
    s_hooks.m_adapter_deadline_hint = SchedulerCapture::adapterDeadline;
    require(s_executor.setHooks(s_hooks) == WL_OK, "deadline hooks failed");
    require(s_executor.start() == WL_OK, "deadline executor start failed");

    waitFor(s_capture.cv, s_capture.mutex,
            [&] { return s_capture.application_fired; },
            "application deadline did not drive owner progress");
    const auto s_application_elapsed =
        s_capture.application_fired_at - s_capture.started_at;
    require(s_application_elapsed >=
                std::chrono::milliseconds(s_capture.application_delay_ms - 5),
            "application timeout fired prematurely");
    require(s_application_elapsed < std::chrono::milliseconds(500),
            "application timeout missed its scheduler deadline");

    if (s_capture.adapter_delay_ms != WL_POLL_NO_DEADLINE_MS) {
        require(s_capture.adapter_fired,
                "adapter policy deadline was not serviced");
        const auto s_adapter_elapsed =
            s_capture.adapter_fired_at - s_capture.started_at;
        require(s_adapter_elapsed >=
                    std::chrono::milliseconds(s_capture.adapter_delay_ms - 5),
                "adapter policy fired prematurely");
        require(s_adapter_elapsed < s_application_elapsed,
                "scheduler did not select the earlier adapter deadline");
    }

    const auto s_idle_progress_calls =
        s_capture.progress_calls.load(std::memory_order_relaxed);
    const auto s_idle_service_calls =
        s_capture.service_calls.load(std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    require(s_capture.progress_calls.load(std::memory_order_relaxed) ==
                s_idle_progress_calls &&
                s_capture.service_calls.load(std::memory_order_relaxed) ==
                    s_idle_service_calls,
            "deadline scheduler busy-spun after all hints became none");
    s_executor.stop();
}

void testApplicationAndAdapterDeadlineScheduling() {
    SchedulerCapture s_application_only;
    runApplicationDeadlineTest(s_application_only);

    SchedulerCapture s_combined;
    s_combined.application_delay_ms = 70;
    s_combined.adapter_delay_ms = 20;
    runApplicationDeadlineTest(s_combined);
}

} // namespace

int main() {
    try {
        testFeedOnlyWakesOwner();
        std::puts("PASS: RX producer only feeds and wakes the owner");
        testLatestLanesCoalesceWithoutCrossMessageLoss();
        std::puts("PASS: keyed latest lanes coalesce without cross-message loss");
        testBoundedLatestAndDeterministicShutdown();
        std::puts("PASS: latest lanes are bounded and shutdown is deterministic");
        testApplicationAndAdapterDeadlineScheduling();
        std::puts("PASS: scheduler merges application and adapter deadlines");
        return 0;
    } catch (const std::exception& s_error) {
        std::fprintf(stderr, "FAIL: %s\n", s_error.what());
        return 1;
    }
}
