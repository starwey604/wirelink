/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_CPP_OPERATION_HPP
#define WIRELINK_CPP_OPERATION_HPP

#include <wirelink/cpp/result.hpp>
#include <wirelink/host/rpc_task.hpp>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <optional>

namespace wirelink {

// Coalesced native notification for language/event-loop integrations. Carries
// no Python objects and never calls user code. stop permanently wakes waiters.
class CompletionSignal {
public:
  void notify() noexcept {
    std::lock_guard lock(mutex_);
    pending_ = true;
    changed_.notify_one();
  }
  bool wait() {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return stopped_ || pending_; });
    pending_ = false;
    return !stopped_;
  }
  void stop() noexcept {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    changed_.notify_all();
  }
private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool pending_{};
  bool stopped_{};
};

namespace detail {
// Generator bridge, submitted once. Session sets endpoint and cancellation
// before publishing it to the owner; neither reference owns the connection.
class AsyncCall : public host::RpcTask {
public:
  void* endpoint{};
  std::function<bool()> request_cancel;
};

template<class T> class OperationState : public AsyncCall {
public:
  virtual T decode_response() = 0;

  void finish(const wl_rpc_completion_t& completion) noexcept final {
    auto result = [&]() -> Result<T> {
      if (completion.status != WL_RPC_SUCCESS) return Error::from_completion(completion);
      try { return decode_response(); }
      catch (const std::bad_alloc&) { return Error::local(WL_ERR_NO_MEM); }
    }();
    std::shared_ptr<CompletionSignal> signal;
    {
      std::lock_guard lock(mutex_);
      result_.emplace(std::move(result));
      signal = signal_;
      changed_.notify_all();
    }
    if (signal) signal->notify();
  }

  bool done() const noexcept {
    std::lock_guard lock(mutex_);
    return result_.has_value();
  }
  void wait() const {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return result_.has_value(); });
  }
  bool wait_for(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [this] { return result_.has_value(); });
  }
  Result<T> result() const {
    wait();
    std::lock_guard lock(mutex_);
    return *result_;
  }
  // One coalescing signal per operation; registration after completion wakes it
  // immediately. An admitted task retains the signal until owner completion.
  void notify_on_completion(std::shared_ptr<CompletionSignal> signal) {
    std::lock_guard lock(mutex_);
    signal_ = std::move(signal);
    if (result_ && signal_) signal_->notify();
  }
private:
  mutable std::mutex mutex_;
  mutable std::condition_variable changed_;
  std::optional<Result<T>> result_;
  std::shared_ptr<CompletionSignal> signal_;
};
} // namespace detail

// Copyable handle to one native operation. Dropping handles does not cancel;
// the session owns admitted work through completion. Results own their data and
// remain readable after close/destruction of the client. cancel requests local
// cancellation only: a peer may already have performed the operation.
template<class T> class [[nodiscard]] Operation {
public:
  explicit Operation(std::shared_ptr<detail::OperationState<T>> state)
      : state_(std::move(state)) {}
  bool done() const noexcept { return state_->done(); }
  bool cancel() const { return state_->request_cancel(); }
  void wait() const { state_->wait(); }
  bool wait_for(std::chrono::milliseconds timeout) const { return state_->wait_for(timeout); }
  Result<T> result() const { return state_->result(); }
  void notify_on_completion(std::shared_ptr<CompletionSignal> signal) const {
    state_->notify_on_completion(std::move(signal));
  }
private:
  std::shared_ptr<detail::OperationState<T>> state_;
};

} // namespace wirelink
#endif
