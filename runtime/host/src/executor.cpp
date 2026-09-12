/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/host/executor.hpp"
#include "wirelink/diagnostics/host_profile.hpp"

#include <chrono>
#include <condition_variable>
#include <system_error>

namespace wirelink::host {
using diagnostics::Scope;
using diagnostics::Stage;
using diagnostics::MutexScope;

namespace {
constexpr std::size_t s_kPollBudget = 64;
thread_local Executor* current_executor{};

wl_rpc_completion_t localFailure(int error) {
    wl_rpc_completion_t result{};
    result.status = error == WL_ERR_CANCELLED ? WL_RPC_CANCELLED : WL_RPC_FAILED;
    result.local_error = error;
    return result;
}
} // namespace

struct Executor::RpcJob {
    Executor* owner{};
    wl_rpc_sync_call_t call{};
    wl_time_ms_t deadline{};
    wl_rpc_call_t handle{};
    bool started{};
    std::shared_ptr<RpcTask> task;
    bool cancel_requested{};
    bool cancel_dispatched{};
    wl_rpc_completion_t result{};
    bool completed{};
    std::condition_variable finished;
};

Executor::~Executor() {
    stop();
}

int Executor::initialize(const wl_config_t& s_config,
                                 const wl_storage_t& s_storage, wl_clock_t clock) {
    if (state() != State::kUninitialized) return WL_ERR_INVALID_STATE;
    if (s_config.max_payload_len > s_kMaximumCommandPayload || clock.now_ms == nullptr) {
        return WL_ERR_INVALID_ARG;
    }

    int s_result = initializeOutbox();
    if (s_result != WL_OK) return s_result;
    s_result = wl_init(m_link, &s_config, &s_storage);
    if (s_result != WL_OK) return s_result;
    m_clock = clock;
    m_state.store(State::kReady, std::memory_order_release);
    return WL_OK;
}

int Executor::initializeOutbox() {
    const wl_outbox_config_t s_outbox_config{
        .slots = m_outbox_slots.data(),
        .slot_count = static_cast<std::uint16_t>(m_outbox_slots.size()),
        .payload_storage = m_outbox_payloads.data(),
        .payload_storage_size = m_outbox_payloads.size(),
        .payload_capacity_per_slot = s_kMaximumCommandPayload,
        .initial_generation = 0,
    };
    return wl_outbox_init(&m_outbox, &s_outbox_config);
}

int Executor::initialize(wl_endpoint_driver_t driver) {
    if (state() != State::kUninitialized) return WL_ERR_INVALID_STATE;
    if (driver.endpoint == nullptr || driver.step == nullptr || driver.close == nullptr ||
        wl_endpoint_link(driver.endpoint) == nullptr ||
        wl_endpoint_rpc_executor(driver.endpoint) != nullptr) return WL_ERR_INVALID_ARG;
    const int error = initializeOutbox();
    if (error != WL_OK) return error;
    if (wl_endpoint_get_clock(driver.endpoint, &m_clock) != WL_OK) return WL_ERR_INVALID_ARG;
    if (const auto* waiter = wl_endpoint_waiter(driver.endpoint); waiter != nullptr) {
        if (waiter->notify == nullptr) return WL_ERR_NOT_SUPPORTED;
        m_platform_waiter = *waiter;
    }
    const int bound = wl_endpoint_set_rpc_executor(driver.endpoint, &m_rpc_executor);
    if (bound != WL_OK) return bound;
    m_driver = driver;
    m_link = wl_endpoint_link(driver.endpoint);
    m_state.store(State::kReady, std::memory_order_release);
    return WL_OK;
}

int Executor::setHooks(const ExecutorHooks& s_hooks) {
    if (state() != State::kReady) return WL_ERR_INVALID_STATE;
    if (m_driver.endpoint != nullptr) return WL_ERR_NOT_SUPPORTED;
    m_hooks = s_hooks;
    return WL_OK;
}

int Executor::setSink(wl_sink_fn s_sink, void* s_user_data) {
    if (state() != State::kReady) return WL_ERR_INVALID_STATE;
    return wl_set_sink(m_link, s_sink, s_user_data);
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
        if (m_driver.endpoint != nullptr) {
            (void)m_driver.close(m_driver.context);
            m_state.store(State::kStopped, std::memory_order_release);
            return;
        }
        auto s_hooks = s_pumpHooks();
        wl_pump_quiesce(&s_hooks);
        (void)wl_set_sink(m_link, nullptr, nullptr);
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
    const int s_result = wl_feed_bytes(m_link, s_data, s_size, &s_accepted);
    if (s_accepted == 0) m_activity.add(ExecutorActivity::feed_without_bytes);
    m_stats.m_feed_bytes.fetch_add(s_accepted, std::memory_order_relaxed);
    if (s_result == WL_ERR_WOULD_BLOCK || s_result == WL_ERR_NO_SPACE) {
        m_stats.m_feed_backpressure.fetch_add(1, std::memory_order_relaxed);
    }

    if (m_producers_in_flight.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        m_producers_in_flight.notify_all();
    }
    // An empty successful feed publishes nothing. A zero-acceptance overflow
    // can still publish RX recovery state and must wake the owner.
    if (s_accepted != 0 || s_result == WL_ERR_WOULD_BLOCK ||
        s_result == WL_ERR_NO_SPACE) notify();
    return s_result;
}

void Executor::notify() noexcept {
    m_activity.add(ExecutorActivity::notifications);
    Scope profile(Stage::wake);
    m_wake_profile.notify();
    m_wake_generation.fetch_add(1, std::memory_order_release);
    if (m_platform_waiter.notify != nullptr) m_platform_waiter.notify(m_platform_waiter.user_data);
    else m_wake.notify();
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
        MutexScope s_lock(m_command_mutex, Stage::latest_submit_wait, Stage::latest_submit_hold);
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
        m_commands_pending.store(true, std::memory_order_release);
        if (s_coalesced != 0U) {
            m_stats.m_latest_coalesced.fetch_add(
                1, std::memory_order_relaxed);
        }
    }

    m_stats.m_latest_submitted.fetch_add(1, std::memory_order_relaxed);
    notify();
    return WL_OK;
}

wl_time_ms_t Executor::nowMs() const noexcept {
    return m_clock.now_ms(m_clock.user_data);
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
    Scope profile(Stage::application);
    auto& s_self = *static_cast<Executor*>(s_user_data);
    s_self.m_application_pending = s_self.m_hooks.m_application_progress(
        s_self.m_hooks.m_user_data, *s_context, s_now_ms);
    return s_self.m_application_pending ? 1U : 0U;
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

wl_pump_event_disposition_t Executor::s_eventBridge(
    void* s_user_data, wl_ctx_t* s_context,
    const wl_event_t* s_event, wl_time_ms_t s_now_ms) noexcept {
    auto& s_self = *static_cast<Executor*>(s_user_data);
    return s_self.m_hooks.m_on_event(s_self.m_hooks.m_user_data, *s_context,
                                     *s_event, s_now_ms);
}

wl_pump_hooks_t Executor::s_pumpHooks() noexcept {
    return wl_pump_hooks_t{
        .adapter_user_data = this,
        .service = m_hooks.m_service != nullptr ? &Executor::s_serviceBridge
                                                : nullptr,
        .quiesce = m_hooks.m_quiesce != nullptr ? &Executor::s_quiesceBridge
                                                : nullptr,
        .adapter_deadline_hint =
            m_hooks.m_adapter_deadline_hint != nullptr
                ? &Executor::s_adapterDeadlineBridge
                : nullptr,
        .application_user_data = this,
        .application_progress =
            m_hooks.m_application_progress != nullptr
                ? &Executor::s_applicationProgressBridge
                : nullptr,
        .application_deadline_hint =
            m_hooks.m_application_deadline_hint != nullptr
                ? &Executor::s_applicationDeadlineBridge
                : nullptr,
        .on_event = m_hooks.m_on_event != nullptr ? &Executor::s_eventBridge
                                                  : nullptr,
    };
}

void Executor::s_run() noexcept {
    current_executor = this;
    auto s_pump_hooks = s_pumpHooks();
    while (!m_stop_requested.load(std::memory_order_acquire)) {
        m_activity.add(ExecutorActivity::passes);
        Scope profile_owner(Stage::owner);
        (void)m_wake.try_wait();
        m_wake_profile.consume(Stage::notify_to_owner);
        const std::uint64_t s_observed_wake =
            m_wake_generation.load(std::memory_order_acquire);
        wl_pump_result_t s_pump_result{};
        const auto rpc_jobs = s_dispatchRpc();
        Scope profile_pump(Stage::pump);
        m_application_pending = false;
        int s_step_result;
        if (m_driver.endpoint != nullptr) {
            s_step_result = m_driver.step(m_driver.context);
            s_pump_result = *wl_endpoint_last_step(m_driver.endpoint);
        } else {
            s_step_result = wl_pump_step(m_link, nowMs(), s_kPollBudget,
                &s_pump_hooks, &s_pump_result);
        }
        profile_pump.finish();
        if (s_step_result != WL_OK) {
            m_stats.m_poll_errors.fetch_add(1, std::memory_order_relaxed);
            if (s_step_result == WL_ERR_IO || s_step_result == WL_ERR_NOT_INITIALIZED) {
                m_stop_error = s_step_result;
                requestStop();
            }
        }
        m_stats.m_rx_events.fetch_add(s_pump_result.rx_events,
                                      std::memory_order_relaxed);
        m_stats.m_poll_errors.fetch_add(s_pump_result.poll_errors,
                                        std::memory_order_relaxed);
        m_stats.m_service_errors.fetch_add(s_pump_result.service_errors,
                                           std::memory_order_relaxed);
        bool s_progress = s_pump_result.progress != 0U;
        m_activity.add(ExecutorActivity::core_events, s_pump_result.events);
        m_activity.add(ExecutorActivity::rx_events, s_pump_result.rx_events);
        if (m_stop_requested.load(std::memory_order_acquire)) break;

        bool latest_progress = false;
        for (std::size_t i = 0; i < s_kLatestDispatchBudget; ++i) {
            if (m_stop_requested.load(std::memory_order_acquire) || !s_dispatchOne()) break;
            latest_progress = true;
        }
        if (latest_progress) {
            s_progress = true;
        }
        if (!s_progress && rpc_jobs == 0)
            m_activity.add(ExecutorActivity::no_reported_work);
        // Raw hooks explicitly report whether another application pass is
        // needed. Consumed events alone are history, not a reason to spin.
        // Custom endpoint drivers may aggregate several steps/peers; preserve
        // their existing progress contract instead of inferring from the last
        // endpoint result that their entire driver is idle.
        const bool application_pending = m_driver.endpoint != nullptr
            ? !m_driver.readiness_complete && s_pump_result.progress != 0U
            : m_application_pending;
        // Without a readiness hint, a legacy service hook may need a follow-up
        // after RX/TX callbacks start adapter work. Do not infer that such an
        // adapter is idle merely because the core has consumed its events.
        const bool legacy_service_progress = m_driver.endpoint == nullptr &&
            m_hooks.m_service != nullptr && m_hooks.m_adapter_deadline_hint == nullptr &&
            s_pump_result.progress != 0U;
        if (application_pending || legacy_service_progress) {
            m_activity.add(ExecutorActivity::continue_progress);
            continue;
        }

        wl_poll_hint_t s_hint{};
        const int s_hint_result = m_driver.endpoint != nullptr
            ? wl_endpoint_get_hint(m_driver.endpoint, &s_hint)
            : wl_pump_get_hint(m_link, nowMs(), &s_pump_hooks, &s_hint);
        if (s_hint_result != WL_OK) {
            m_stats.m_poll_errors.fetch_add(1, std::memory_order_relaxed);
        } else if (s_hint.work_pending != 0) {
            m_activity.add(ExecutorActivity::continue_hint);
            continue;
        }

        // A send accepted after this pass's service may have started async
        // adapter work. Give service one opportunity before sleeping. If it
        // is still blocked, dispatch makes no progress on the next pass and
        // we wait for readiness instead of spinning on a nonempty outbox.
        if (latest_progress) {
            m_activity.add(ExecutorActivity::continue_progress);
            continue;
        }

        std::uint32_t s_next_deadline =
            s_hint_result == WL_OK ? s_hint.next_deadline_ms
                                   : WL_POLL_NO_DEADLINE_MS;
        if (s_next_deadline == 0) {
            m_activity.add(ExecutorActivity::continue_deadline);
            continue;
        }

        if (m_stop_requested.load(std::memory_order_acquire) ||
            m_wake_generation.load(std::memory_order_acquire) !=
                s_observed_wake) {
            m_activity.add(ExecutorActivity::continue_notification);
            continue;
        }
        profile_owner.finish();
        Scope profile_wait(Stage::wait);
        m_activity.add(ExecutorActivity::waits);
        if (m_platform_waiter.wait != nullptr) {
            const int waited = m_platform_waiter.wait(m_platform_waiter.user_data, s_next_deadline);
            if (waited != WL_OK && waited != WL_ERR_NO_DATA) {
                m_stats.m_service_errors.fetch_add(1, std::memory_order_relaxed);
                if (waited != WL_ERR_CANCELLED) m_stop_error = waited;
                requestStop();
            }
        } else if (s_next_deadline != WL_POLL_NO_DEADLINE_MS) {
            (void)m_wake.wait_for(
                std::chrono::milliseconds(s_next_deadline));
        } else {
            m_wake.wait();
        }
    }

    s_shutdownOnOwner();
    current_executor = nullptr;
    m_state.store(State::kStopped, std::memory_order_release);
}

bool Executor::s_dispatchOne() noexcept {
    Scope profile(Stage::command);
    if (!m_commands_pending.load(std::memory_order_acquire)) return false;
    // acquire_copy initializes exactly payload_length bytes; no full 512-byte
    // zero fill is needed for an empty queue or a short command.
    std::array<std::uint8_t, s_kMaximumCommandPayload> s_payload;
    wl_outbox_item_t s_item{};
    {
        MutexScope s_lock(m_command_mutex, Stage::latest_take_wait, Stage::latest_take_hold);
        const int s_acquired = wl_outbox_acquire_copy(
            &m_outbox, s_payload.data(), s_payload.size(), &s_item);
        if (s_acquired == WL_ERR_NO_DATA) return false;
        if (s_acquired != WL_OK) {
            m_stats.m_latest_failed.fetch_add(1,
                                               std::memory_order_relaxed);
            return false;
        }
    }

    m_activity.add(ExecutorActivity::latest_attempts);
    const int s_result = wl_send_unreliable(
        m_link, s_item.message_id, s_payload.data(),
        s_item.payload_length);
    const bool s_deferred = s_result == WL_ERR_BUSY ||
                            s_result == WL_ERR_WOULD_BLOCK ||
                            s_result == WL_ERR_NO_SPACE;
    {
        MutexScope s_lock(m_command_mutex, Stage::latest_finish_wait, Stage::latest_finish_hold);
        (void)wl_outbox_complete(
            &m_outbox, &s_item,
            s_result == WL_OK
                ? WL_OUTBOX_ACCEPTED
                : (s_deferred ? WL_OUTBOX_DEFERRED
                              : WL_OUTBOX_REJECTED));
        wl_outbox_stats_t s_outbox_stats{};
        (void)wl_outbox_get_stats(&m_outbox, &s_outbox_stats);
        m_commands_pending.store(s_outbox_stats.depth != 0, std::memory_order_release);
    }
    if (s_result == WL_OK) {
        m_activity.add(ExecutorActivity::latest_sent);
        m_stats.m_latest_dispatched.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    if (s_deferred) {
        m_activity.add(ExecutorActivity::latest_deferred);
        return false;
    }

    m_stats.m_latest_failed.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void Executor::s_shutdownOnOwner() noexcept {
    m_accepting.store(false, std::memory_order_release);
    auto s_pump_hooks = s_pumpHooks();
    if (m_driver.endpoint != nullptr) {
        (void)m_driver.close(m_driver.context); // Quiesce, then finish active RPCs.
        s_cancelQueuedRpc();
    } else wl_pump_quiesce(&s_pump_hooks);

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
        m_commands_pending.store(false, std::memory_order_release);
        if (s_cancelled != 0) {
            m_stats.m_latest_cancelled.fetch_add(s_cancelled,
                                                 std::memory_order_relaxed);
        }
    }
    (void)wl_set_sink(m_link, nullptr, nullptr);
}

wl_rpc_completion_t Executor::s_invokeRpc(void* context,
    const wl_rpc_sync_call_t* call, std::uint32_t timeout_ms) {
    auto& self = *static_cast<Executor*>(context);
    if (current_executor == &self) return localFailure(WL_ERR_REENTRANT);
    if (call == nullptr || call->submit == nullptr || timeout_ms == 0 || timeout_ms > INT32_MAX)
        return localFailure(WL_ERR_INVALID_ARG);
    RpcJob job;
    job.owner = &self;
    job.call = *call;
    {
        MutexScope lock(self.m_rpc_mutex, Stage::rpc_admit_wait, Stage::rpc_admit_hold);
        if (!self.m_accepting.load(std::memory_order_acquire)) return localFailure(WL_ERR_CANCELLED);
        RpcJob** free = nullptr;
        for (auto& slot : self.m_rpc_jobs) if (slot == nullptr) { free = &slot; break; }
        if (free == nullptr) {
            self.m_stats.m_rpc_queue_full.fetch_add(1, std::memory_order_relaxed);
            return localFailure(WL_ERR_BUSY);
        }
        job.deadline = self.nowMs() + timeout_ms;
        *free = &job;
        self.m_rpc_pending.store(true, std::memory_order_release);
        self.m_stats.m_rpc_submitted.fetch_add(1, std::memory_order_relaxed);
    }
    self.notify();
    std::unique_lock lock(self.m_rpc_mutex);
    job.finished.wait(lock, [&job] { return job.completed; });
    return job.result;
}

void Executor::s_finishRpc(void* context, const wl_rpc_completion_t* result) {
    auto& job = *static_cast<RpcJob*>(context);
    std::shared_ptr<RpcTask> task;
    wl_rpc_completion_t completion{};
    {
        MutexScope lock(job.owner->m_rpc_mutex, Stage::rpc_finish_wait, Stage::rpc_finish_hold);
        job.result = *result;
        if (job.result.status == WL_RPC_CANCELLED && job.owner->m_stop_error != WL_OK) {
            job.result.status = WL_RPC_FAILED;
            job.result.local_error = job.owner->m_stop_error;
        }
        job.completed = true;
        job.owner->m_stats.m_rpc_completed.fetch_add(1, std::memory_order_relaxed);
        job.owner->m_activity.add(ExecutorActivity::rpc_completions);
        for (auto& slot : job.owner->m_rpc_jobs) if (slot == &job) { slot = nullptr; break; }
        task = std::move(job.task);
        if (task) completion = job.result;
        // A synchronous caller cannot destroy its stack CV until this
        // notification returns and the shared admission mutex is released.
        else job.finished.notify_one();
    }
    if (task) {
        delete &job;
        task->finish(completion);
    }
}

std::size_t Executor::s_dispatchRpc() noexcept {
    if (!m_rpc_pending.load(std::memory_order_acquire)) return 0;
    std::array<RpcJob*, 8> pending{};
    std::array<bool, 8> cancelling{};
    std::size_t count{};
    {
        MutexScope lock(m_rpc_mutex, Stage::rpc_collect_wait, Stage::rpc_collect_hold);
        for (auto* job : m_rpc_jobs) {
            if (job == nullptr) continue;
            if (job->cancel_requested && !job->cancel_dispatched) {
                job->cancel_dispatched = true;
                cancelling[count] = true;
                pending[count++] = job;
                continue;
            }
            if (job->started) continue;
            job->started = true;
            pending[count++] = job;
        }
        // Admission publishes under this same mutex. Clear only after every
        // currently queued job is marked started, so an admission collected in
        // this batch cannot leave a stale hint for an empty next collection.
        // Later admissions acquire the mutex and publish true after this reset.
        m_rpc_pending.store(false, std::memory_order_release);
    }
    // The owner alone completes jobs. Callers keep their stack job alive until
    // completion; submit/callbacks run outside the admission mutex.
    m_activity.add(ExecutorActivity::rpc_batches, count != 0);
    m_activity.add(ExecutorActivity::rpc_empty_collections, count == 0);
    m_activity.add(ExecutorActivity::rpc_jobs, count);
    for (std::size_t i = 0; i < count; ++i) {
        auto* job = pending[i];
        const auto task = job->task; // Retain through an inline completion callback.
        if (cancelling[i]) {
            if (job->started) (void)task->cancel(job->handle);
            else {
                const auto result = localFailure(WL_ERR_CANCELLED);
                s_finishRpc(job, &result);
            }
            continue;
        }
        const int error = task
            ? task->submit(job->deadline, &Executor::s_finishRpc, job, &job->handle)
            : job->call.submit(job->call.context, job->deadline,
                &Executor::s_finishRpc, job, &job->handle);
        if (error != WL_OK) {
            auto result = localFailure(error);
            if (error == WL_ERR_TIMEOUT) {
                result.status = WL_RPC_TIMED_OUT;
                result.local_error = WL_OK;
            }
            s_finishRpc(job, &result);
        }
    }
    return count;
}

int Executor::submitRpc(std::shared_ptr<RpcTask> task, std::uint32_t timeout_ms) noexcept try {
    if (!task || timeout_ms == 0 || timeout_ms > INT32_MAX)
        return WL_ERR_INVALID_ARG;
    if (m_driver.endpoint == nullptr) return WL_ERR_INVALID_STATE;
    auto job = std::unique_ptr<RpcJob>(new (std::nothrow) RpcJob);
    if (!job) return WL_ERR_NO_MEM;
    job->owner = this;
    job->task = std::move(task);
    {
        std::lock_guard lock(m_rpc_mutex);
        if (!m_accepting.load(std::memory_order_acquire)) return WL_ERR_NOT_INITIALIZED;
        RpcJob** free = nullptr;
        for (auto& slot : m_rpc_jobs) {
            if (slot && slot->task == job->task) return WL_ERR_INVALID_STATE;
            if (!slot && !free) free = &slot;
        }
        if (!free) {
            m_stats.m_rpc_queue_full.fetch_add(1, std::memory_order_relaxed);
            return WL_ERR_QUEUE_FULL;
        }
        job->deadline = nowMs() + timeout_ms;
        *free = job.release();
        m_rpc_pending.store(true, std::memory_order_release);
        m_stats.m_rpc_submitted.fetch_add(1, std::memory_order_relaxed);
    }
    notify();
    return WL_OK;
} catch (const std::bad_alloc&) {
    return WL_ERR_NO_MEM;
} catch (const std::system_error&) {
    return WL_ERR_IO;
}

bool Executor::cancelRpc(const RpcTask* task) noexcept {
    if (!task) return false;
    {
        std::lock_guard lock(m_rpc_mutex);
        if (!m_accepting.load(std::memory_order_acquire)) return false;
        bool found = false;
        for (auto* job : m_rpc_jobs) {
            if (job && job->task.get() == task && !job->cancel_requested) {
                job->cancel_requested = true;
                found = true;
                break;
            }
        }
        if (!found) return false;
        m_rpc_pending.store(true, std::memory_order_release);
    }
    notify();
    return true;
}

void Executor::s_cancelQueuedRpc() noexcept {
    for (std::size_t i = 0; i < m_rpc_jobs.size(); ++i) {
        RpcJob* job;
        {
            std::lock_guard lock(m_rpc_mutex);
            job = m_rpc_jobs[i];
        }
        if (job != nullptr) {
            const auto result = localFailure(WL_ERR_CANCELLED);
            s_finishRpc(job, &result);
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
        .m_rpc_submitted = m_stats.m_rpc_submitted.load(std::memory_order_relaxed),
        .m_rpc_completed = m_stats.m_rpc_completed.load(std::memory_order_relaxed),
        .m_rpc_queue_full = m_stats.m_rpc_queue_full.load(std::memory_order_relaxed),
    };
}

} // namespace wirelink::host
