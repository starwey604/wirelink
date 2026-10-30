/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/host/executor.hpp"
#include "wirelink/wait.h"
#include <algorithm>
#include <new>
#include <system_error>

namespace wirelink::host {
Executor::~Executor() {
  if (stop() == WL_ERR_REENTRANT) std::terminate();
}
wl_err_t Executor::initialize(wl_kernel_t &core, ExecutorIo io, wl_clock_t clock) {
  std::lock_guard lock(gate_);
  if (state_ != State::uninitialized || !clock.now_ms ||
      ((io.wait == nullptr) != (io.notify == nullptr))) return WL_ERR_INVALID_ARG;
  core_ = &core; io_ = io; clock_ = clock; state_ = State::ready;
  return WL_OK;
}
wl_err_t Executor::start() {
  std::lock_guard lock(gate_);
  if (state_ != State::ready) return WL_ERR_INVALID_STATE;
  try {
    state_ = State::running;
    thread_ = std::thread([this] { run(); });
    return WL_OK;
  } catch (const std::bad_alloc &) {
    state_ = State::ready;
    return WL_ERR_NO_MEM;
  } catch (const std::system_error &) {
    state_ = State::ready;
    return WL_ERR_IO;
  }
}
void Executor::wake_locked() noexcept {
  if (io_.notify) io_.notify(io_.user);
  else event_.notify();
}
wl_err_t Executor::post(Work work, Reject reject) {
  if (!work) return WL_ERR_INVALID_ARG;
  std::lock_guard lock(gate_);
  if (state_ != State::running) return WL_ERR_NOT_INITIALIZED;
  if (commands_.size() == command_capacity) return WL_ERR_QUEUE_FULL;
  try { commands_.push_back({std::move(work), std::move(reject)}); }
  catch (const std::bad_alloc &) { return WL_ERR_NO_MEM; }
  // Synchronize notification with stop so I/O cannot close before this call.
  wake_locked();
  return WL_OK;
}
void Executor::notify() noexcept {
  std::lock_guard lock(gate_);
  if (state_ == State::running || state_ == State::stopping) {
    event_.notify();
    if (io_.notify) io_.notify(io_.user);
  }
}
void Executor::request_stop() noexcept {
  std::lock_guard lock(gate_);
  if (state_ == State::running) {
    state_ = State::stopping;
    wake_locked();
  }
}
wl_err_t Executor::stop() noexcept {
  if (on_owner_thread()) return WL_ERR_REENTRANT;
  std::lock_guard join(join_);
  request_stop();
  if (thread_.joinable()) thread_.join();
  return WL_OK;
}
bool Executor::on_owner_thread() const noexcept {
  std::lock_guard lock(gate_);
  return owner_ == std::this_thread::get_id();
}
Executor::State Executor::state() const noexcept {
  std::lock_guard lock(gate_);
  return state_;
}
bool Executor::stopping() const noexcept {
  std::lock_guard lock(gate_);
  return state_ != State::running;
}
bool Executor::commands(bool reject) {
  for (unsigned i = 0; i < command_budget; ++i) {
    Command command;
    {
      std::lock_guard lock(gate_);
      if (commands_.empty()) return false;
      command = std::move(commands_.front()); commands_.pop_front();
      reject |= state_ != State::running;
    }
    if (reject) {
      if (command.reject) command.reject(WL_ERR_NOT_INITIALIZED);
    } else command.work(now());
  }
  std::lock_guard lock(gate_);
  return !commands_.empty();
}
void Executor::run() noexcept {
  { std::lock_guard lock(gate_); owner_ = std::this_thread::get_id(); }
  auto failure = []() noexcept -> wl_err_t {
    try { throw; }
    catch (const std::bad_alloc &) { return WL_ERR_NO_MEM; }
    catch (...) { return WL_ERR_IO; }
  };
  auto wait = [&](uint32_t limit) -> wl_err_t {
    try {
      if (io_.wait) return io_.wait(io_.user, limit);
      if (limit == UINT32_MAX) { event_.wait(); return WL_OK; }
      return event_.wait_for(std::chrono::milliseconds(limit)) ? WL_OK : WL_ERR_NO_DATA;
    } catch (...) { return failure(); }
  };
  while (!stopping()) {
    bool more = commands(false);
    uint32_t sampled = now();
    wl_err_t error = WL_OK;
    try { if (io_.service) error = io_.service(io_.user, sampled); }
    catch (...) { error = failure(); }
    const auto core_error = wl_kernel_step(core_, sampled, WL_KERNEL_BUDGET);
    if (error != WL_OK) last_error_.store(error);
    if (core_error != WL_OK) last_error_.store(core_error);
    if (error == WL_ERR_IO || error == WL_ERR_NO_MEM || core_error == WL_ERR_IO) { request_stop(); continue; }
    sampled = now();
    int have_deadline = 0; uint32_t deadline = 0;
    bool ready = wl_kernel_ready(core_, sampled, &have_deadline, &deadline) != 0;
    auto io = wl_io_readiness_idle();
    try { if (io_.readiness) io = io_.readiness(io_.user, sampled); }
    catch (...) { last_error_.store(failure()); request_stop(); continue; }
    uint32_t limit = have_deadline ? deadline - sampled : UINT32_MAX;
    limit = std::min(limit, io.next_deadline_ms);
    { std::lock_guard lock(gate_); more |= !commands_.empty() || state_ != State::running; }
    if (more || ready || io.runnable) continue;
    error = wait(wl_wait_remaining(limit, sampled, now()));
    if (error != WL_OK && error != WL_ERR_NO_DATA) {
      last_error_.store(error); request_stop();
    }
  }
  while (commands(true)) {}
  (void)wl_kernel_close(core_);
  if (io_.stop) io_.stop(io_.user);
  // Logical terminals and physical leases remain separate. Join cannot release
  // storage while any real completion or RX borrow is still outstanding.
  do {
    const auto error = wl_kernel_step(core_, now(), WL_KERNEL_BUDGET);
    if (error != WL_OK) last_error_.store(error);
    if (wl_kernel_quiescent(core_)) break;
    int have_deadline = 0; uint32_t deadline = 0;
    if (wl_kernel_ready(core_, now(), &have_deadline, &deadline)) continue;
    // A stopped I/O wait can reject further socket operations. Keep storage
    // alive and wait for a real producer notification rather than spinning.
    const auto waited = wait(UINT32_MAX);
    if (waited != WL_OK && waited != WL_ERR_NO_DATA) event_.wait();
  } while (true);
  { std::lock_guard lock(gate_); owner_ = {}; state_ = State::stopped; }
}
} // namespace wirelink::host
