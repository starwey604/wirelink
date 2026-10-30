/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_HOST_EXECUTOR_HPP
#define WIRELINK_HOST_EXECUTOR_HPP
#include "wirelink/kernel.h"
#include "wirelink/clock.h"
#include "wirelink/io_readiness.h"
#include "wirelink/host/clock.hpp"
#include "wirelink/detail/coalescing_event.hpp"
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace wirelink::host {
struct ExecutorIo {
  void *user{};
  wl_err_t (*service)(void *, uint32_t){};
  wl_io_readiness_t (*readiness)(void *, uint32_t){};
  wl_err_t (*wait)(void *, uint32_t){};
  // Latched, nonblocking, thread-safe; must not reenter Executor or callbacks.
  void (*notify)(void *){};
  // Owner-only: stop/join actual I/O producers and publish final completion.
  // Never synthesize physical drain. Storage remains alive until owner join.
  void (*stop)(void *){};
};
// A thread/wait wrapper around the one C kernel. No RPC table, reply cache,
// application service scheduler, protocol timer or payload storage is mirrored.
class Executor {
public:
  static constexpr size_t command_capacity = 32;
  static constexpr unsigned command_budget = 8;
  enum class State { uninitialized, ready, running, stopping, stopped };
  using Work = std::function<void(uint32_t)>;
  using Reject = std::function<void(wl_err_t)>;
  Executor() = default;
  ~Executor();
  Executor(const Executor &) = delete;
  Executor &operator=(const Executor &) = delete;
  // Binding alone does not transfer ownership: the caller owns the kernel
  // until start succeeds, and must keep all storage alive through stop/join.
  wl_err_t initialize(wl_kernel_t &, ExecutorIo = {}, wl_clock_t = monotonic_clock());
  wl_err_t start();
  // Success owns one command, whose work or shutdown rejection runs on owner.
  // Functions must not throw, block or call stop. Core admission is separate.
  wl_err_t post(Work, Reject = {});
  void notify() noexcept;
  void request_stop() noexcept;
  // Owner callbacks may request_stop, but cannot stop/join or destroy Executor.
  wl_err_t stop() noexcept;
  bool on_owner_thread() const noexcept;
  State state() const noexcept;
  wl_err_t last_error() const noexcept { return last_error_.load(); }
private:
  struct Command { Work work; Reject reject; };
  void wake_locked() noexcept;
  bool stopping() const noexcept;
  bool commands(bool reject);
  void run() noexcept;
  uint32_t now() const noexcept { return clock_.now_ms(clock_.user_data); }
  wl_kernel_t *core_{};
  ExecutorIo io_{};
  wl_clock_t clock_{};
  mutable std::mutex gate_;
  std::mutex join_;
  std::deque<Command> commands_;
  State state_{State::uninitialized};
  std::thread thread_;
  std::thread::id owner_{};
  detail::CoalescingEvent event_;
  std::atomic<wl_err_t> last_error_{WL_OK};
};
} // namespace wirelink::host
#endif
