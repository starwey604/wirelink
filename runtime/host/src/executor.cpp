/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/host/executor.hpp"

#include <chrono>
#include <cstring>
#include <system_error>

namespace wirelink::host {

namespace {
constexpr std::size_t s_kPollBudget = 64;

bool s_isExpectedServiceResult(int s_result) {
    return s_result == WL_OK || s_result == WL_ERR_NO_DATA ||
           s_result == WL_ERR_WOULD_BLOCK;
}
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

    const int s_result = wl_init(&m_context, &s_config, &s_storage);
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
        if (m_hooks.m_quiesce != nullptr) {
            m_hooks.m_quiesce(m_hooks.m_user_data);
        }
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
        LatestLane* s_lane = nullptr;
        for (auto& s_candidate : m_latest_lanes) {
            if (s_candidate.m_valid &&
                s_candidate.m_command.m_message_id == s_message_id) {
                s_lane = &s_candidate;
                m_stats.m_latest_coalesced.fetch_add(
                    1, std::memory_order_relaxed);
                break;
            }
        }
        if (s_lane == nullptr) {
            for (auto& s_candidate : m_latest_lanes) {
                if (!s_candidate.m_valid) {
                    s_lane = &s_candidate;
                    break;
                }
            }
        }
        if (s_lane == nullptr) {
            m_stats.m_latest_queue_full.fetch_add(1,
                                                  std::memory_order_relaxed);
            return WL_ERR_QUEUE_FULL;
        }

        s_lane->m_command.m_generation = m_next_generation++;
        if (m_next_generation == 0) m_next_generation = 1;
        s_lane->m_command.m_message_id = s_message_id;
        s_lane->m_command.m_payload_size =
            static_cast<std::uint16_t>(s_payload_size);
        if (s_payload_size != 0) {
            std::memcpy(s_lane->m_command.m_payload.data(), s_payload,
                        s_payload_size);
        }
        s_lane->m_valid = true;
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

void Executor::s_run() noexcept {
    while (!m_stop_requested.load(std::memory_order_acquire)) {
        for (std::size_t s_index = 0;
             s_index < s_kPollBudget && m_wake.try_acquire(); ++s_index) {
        }
        const std::uint64_t s_observed_wake =
            m_wake_generation.load(std::memory_order_acquire);
        const wl_time_ms_t s_now = s_nowMs();

        if (m_hooks.m_service != nullptr) {
            const int s_result = m_hooks.m_service(m_hooks.m_user_data);
            if (!s_isExpectedServiceResult(s_result)) {
                m_stats.m_service_errors.fetch_add(1, std::memory_order_relaxed);
            }
        }

        bool s_progress = s_pollEvents(s_now);
        if (m_stop_requested.load(std::memory_order_acquire)) break;

        if (m_hooks.m_application_progress != nullptr &&
            m_hooks.m_application_progress(m_hooks.m_user_data, m_context,
                                             s_nowMs())) {
            s_progress = true;
        }
        if (m_stop_requested.load(std::memory_order_acquire)) break;

        if (s_dispatchOne()) {
            s_progress = true;
        }
        if (s_progress) continue;

        wl_poll_hint_t s_hint{};
        const wl_time_ms_t s_hint_now = s_nowMs();
        const int s_hint_result =
            wl_poll_get_hint(&m_context, s_hint_now, &s_hint);
        if (s_hint_result != WL_OK) {
            m_stats.m_poll_errors.fetch_add(1, std::memory_order_relaxed);
        } else if (s_hint.work_pending != 0) {
            continue;
        }

        std::uint32_t s_next_deadline =
            s_hint_result == WL_OK ? s_hint.next_deadline_ms
                                   : WL_POLL_NO_DEADLINE_MS;
        if (m_hooks.m_application_deadline_hint != nullptr) {
            const std::uint32_t s_application_deadline =
                m_hooks.m_application_deadline_hint(m_hooks.m_user_data,
                                                    s_hint_now);
            if (s_application_deadline < s_next_deadline) {
                s_next_deadline = s_application_deadline;
            }
        }
        if (m_hooks.m_adapter_deadline_hint != nullptr) {
            const std::uint32_t s_adapter_deadline =
                m_hooks.m_adapter_deadline_hint(m_hooks.m_user_data,
                                                s_hint_now);
            if (s_adapter_deadline < s_next_deadline) {
                s_next_deadline = s_adapter_deadline;
            }
        }
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

bool Executor::s_pollEvents(wl_time_ms_t s_now_ms) noexcept {
    bool s_progress = false;
    for (std::size_t s_index = 0; s_index < s_kPollBudget; ++s_index) {
        wl_event_t s_event{};
        const int s_result = wl_poll(&m_context, s_now_ms, &s_event);
        if (s_result == WL_ERR_NO_DATA) break;
        if (s_result != WL_OK) {
            m_stats.m_poll_errors.fetch_add(1, std::memory_order_relaxed);
            s_progress = true;
            continue;
        }

        s_progress = true;
        s_handleEvent(s_event);
    }
    return s_progress;
}

bool Executor::s_dispatchOne() noexcept {
    Command s_latest{};
    if (!s_peekLatest(s_latest)) return false;

    const int s_result = wl_send_unreliable(
        &m_context, s_latest.m_message_id, s_latest.m_payload.data(),
        s_latest.m_payload_size);
    if (s_result == WL_OK) {
        s_removeLatest(s_latest.m_generation);
        m_stats.m_latest_dispatched.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    if (s_result == WL_ERR_BUSY || s_result == WL_ERR_WOULD_BLOCK) {
        return false;
    }

    s_removeLatest(s_latest.m_generation);
    m_stats.m_latest_failed.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void Executor::s_handleEvent(const wl_event_t& s_event) noexcept {
    if (s_event.type == WL_EVT_UNRELIABLE_RX ||
        s_event.type == WL_EVT_RELIABLE_RX) {
        m_stats.m_rx_events.fetch_add(1, std::memory_order_relaxed);
        if (m_hooks.m_on_event != nullptr) {
            m_hooks.m_on_event(m_hooks.m_user_data, m_context, s_event);
        } else {
            wl_event_release(&m_context, &s_event);
        }
        return;
    }

    if (s_event.type != WL_EVT_TX_SUCCESS &&
        s_event.type != WL_EVT_TX_TIMEOUT &&
        s_event.type != WL_EVT_TX_FAILED) {
        return;
    }

    // Internal control-unit sends (for example a reliable RX ACK) have no
    // user-visible handle and therefore do not belong to a generated RPC
    // client transaction.
    if (s_event.handle == 0) return;

    // Generated RPC runtimes must see terminal TX events before the handle is
    // taken. They use the handle to move LINK_PENDING requests forward.
    if (m_hooks.m_on_event != nullptr) {
        m_hooks.m_on_event(m_hooks.m_user_data, m_context, s_event);
    }

    wl_tx_result_t s_ignored{};
    (void)wl_tx_take(&m_context, s_event.handle, &s_ignored);
}

void Executor::s_shutdownOnOwner() noexcept {
    m_accepting.store(false, std::memory_order_release);
    if (m_hooks.m_quiesce != nullptr) {
        m_hooks.m_quiesce(m_hooks.m_user_data);
    }

    std::uint32_t s_producers =
        m_producers_in_flight.load(std::memory_order_acquire);
    while (s_producers != 0) {
        m_producers_in_flight.wait(s_producers, std::memory_order_acquire);
        s_producers = m_producers_in_flight.load(std::memory_order_acquire);
    }

    if (m_hooks.m_service != nullptr) {
        const int s_service_result = m_hooks.m_service(m_hooks.m_user_data);
        if (!s_isExpectedServiceResult(s_service_result)) {
            m_stats.m_service_errors.fetch_add(1, std::memory_order_relaxed);
        }
    }

    {
        std::lock_guard<std::mutex> s_lock(m_command_mutex);
        std::uint64_t s_cancelled{};
        for (auto& s_lane : m_latest_lanes) {
            if (s_lane.m_valid) {
                s_lane.m_valid = false;
                ++s_cancelled;
            }
        }
        if (s_cancelled != 0) {
            m_stats.m_latest_cancelled.fetch_add(s_cancelled,
                                                 std::memory_order_relaxed);
        }
    }
    (void)wl_set_sink(&m_context, nullptr, nullptr);
}

bool Executor::s_peekLatest(Command& s_command) noexcept {
    std::lock_guard<std::mutex> s_lock(m_command_mutex);
    for (std::size_t s_offset = 0; s_offset < m_latest_lanes.size();
         ++s_offset) {
        const std::size_t s_index =
            (m_latest_cursor + s_offset) % m_latest_lanes.size();
        if (!m_latest_lanes[s_index].m_valid) continue;
        s_command = m_latest_lanes[s_index].m_command;
        m_latest_cursor = (s_index + 1) % m_latest_lanes.size();
        return true;
    }
    return false;
}

void Executor::s_removeLatest(std::uint64_t s_generation) noexcept {
    std::lock_guard<std::mutex> s_lock(m_command_mutex);
    for (auto& s_lane : m_latest_lanes) {
        if (s_lane.m_valid &&
            s_lane.m_command.m_generation == s_generation) {
            s_lane.m_valid = false;
            return;
        }
    }
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
