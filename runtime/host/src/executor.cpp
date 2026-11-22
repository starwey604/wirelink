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
wl_err_t Executor::initialize(wl_node_t &node, wl_clock_t clock) {
  std::lock_guard lock(gate_);
  if (state_ != State::uninitialized || !clock.now_ms || !node.core.port)
    return WL_ERR_INVALID_ARG;
  if (node.transport && node.transport->ops->wait) {
    if (!node.transport->ops->notify) return WL_ERR_INVALID_ARG;
    waitable_ = node.transport;
  }
  node_ = &node;
  clock_ = clock;
  state_ = State::ready;
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
  event_.notify();
  if (waitable_) waitable_->ops->notify(waitable_);
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
    wake_locked();
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
  return close_result_;
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
  auto wait = [&](uint32_t maximum) -> wl_err_t {
    const auto sampled = now();
    const auto readiness = wl_node_readiness(node_, sampled);
    if (readiness.runnable) return WL_OK;
    const auto current = now();
    const auto limit = wl_wait_remaining(std::min(readiness.next_deadline_ms, maximum), sampled, current);
    if (waitable_ && !node_->transport_stopped)
      return wl_node_wait(node_, current, limit);
    if (limit == UINT32_MAX) event_.wait();
    else if (limit) (void)event_.wait_for(std::chrono::milliseconds(limit));
    return WL_OK;
  };
  while (!stopping()) {
    bool more = commands(false);
    wl_err_t error;
    try { error = wl_node_step(node_, now()); }
    catch (...) { error = failure(); }
    if (error != WL_OK) last_error_.store(error);
    if (error == WL_ERR_IO || error == WL_ERR_NO_MEM) { request_stop(); continue; }
    { std::lock_guard lock(gate_); more |= !commands_.empty() || state_ != State::running; }
    if (more) continue;
    try { error = wait(UINT32_MAX); }
    catch (...) { error = failure(); }
    if (error != WL_OK) { last_error_.store(error); request_stop(); }
  }
  while (commands(true)) {}
  for (;;) {
    try { close_result_ = wl_node_close(node_, now()); }
    catch (...) { close_result_ = failure(); }
    if (close_result_ == WL_OK) break;
    if (close_result_ != WL_ERR_BUSY && close_result_ != WL_ERR_WOULD_BLOCK) {
      last_error_.store(close_result_);
      break;
    }
    // Stop may publish the last physical completion from a different context.
    try {
      if (wait(1) != WL_OK) (void)event_.wait_for(std::chrono::milliseconds(1));
    } catch (...) { last_error_.store(failure()); (void)event_.wait_for(std::chrono::milliseconds(1)); }
  }
  { std::lock_guard lock(gate_); owner_ = {}; state_ = State::stopped; }
}
} // namespace wirelink::host
