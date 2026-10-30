/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/host/executor.hpp"
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
using wirelink::host::Executor;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::abort(); } } while (0)
extern "C" wl_kernel_t *owner_pass_prepare(wl_packet_notify_fn, void *);
extern "C" void owner_pass_inject(void);
extern "C" unsigned owner_pass_received(void);
struct Fixture {
  Executor executor;
  wl_kernel_t *core{};
  std::mutex gate;
  std::condition_variable wake;
  bool entered{}, released{};
  std::atomic<unsigned> done{};
  unsigned since_service{}, maximum{}, services{}, rejected{}, stop_calls{};
  std::array<unsigned, 32> observed{};
  static void notify(void *user) { static_cast<Fixture *>(user)->executor.notify(); }
  static wl_err_t service(void *user, uint32_t) {
    auto &f = *static_cast<Fixture *>(user);
    if (f.since_service > f.maximum) f.maximum = f.since_service;
    f.since_service = 0; ++f.services; return WL_OK;
  }
  static void stop(void *user) { ++static_cast<Fixture *>(user)->stop_calls; }
  void start() {
    core = owner_pass_prepare(notify, this);
    wirelink::host::ExecutorIo io; io.user = this; io.service = service; io.stop = stop;
    CHECK(executor.initialize(*core, io) == WL_OK && executor.start() == WL_OK);
    CHECK(executor.post([this](uint32_t) {
      ++since_service; // The test-only barrier is the first command in a batch.
      std::unique_lock lock(gate); entered = true; wake.notify_one();
      wake.wait(lock, [this] { return released; });
    }) == WL_OK);
    std::unique_lock lock(gate); CHECK(wake.wait_for(lock, 5s, [this] { return entered; }));
  }
  void release() { { std::lock_guard lock(gate); released = true; } wake.notify_all(); }
  ~Fixture() { release(); (void)executor.stop(); }
};
static void available_burst() {
  Fixture f; f.start();
  for (unsigned i = 0; i < Executor::command_capacity; ++i) {
    CHECK(f.executor.post([&f, i](uint32_t) {
      CHECK(f.executor.on_owner_thread()); ++f.since_service;
      f.observed[i] = owner_pass_received();
      if (f.done.fetch_add(1) + 1 == Executor::command_capacity) {
        std::lock_guard lock(f.gate); f.wake.notify_one();
      }
    }, [&f](wl_err_t) { ++f.rejected; }) == WL_OK);
  }
  CHECK(f.executor.post([](uint32_t) {}) == WL_ERR_QUEUE_FULL);
  owner_pass_inject(); f.release();
  bool complete;
  { std::unique_lock lock(f.gate); complete = f.wake.wait_for(lock, 5s,
      [&f] { return f.done.load() == Executor::command_capacity; }); }
  CHECK(f.executor.stop() == WL_OK); CHECK(complete);
  CHECK(f.executor.last_error() == WL_OK && wl_kernel_quiescent(f.core));
  CHECK(f.rejected == 0 && f.stop_calls == 1 && f.maximum == Executor::command_budget);
  for (unsigned i = 0; i < Executor::command_budget - 1; ++i) CHECK(f.observed[i] == 0);
  for (unsigned i = Executor::command_budget - 1; i < Executor::command_capacity; ++i) CHECK(f.observed[i] == 1);
  std::printf("{\"format\":\"wirelink-owner-kernel-v1\",\"case\":\"kernel_command_burst\",\"budget\":%u,"
    "\"delivered\":%u,\"received\":%u,\"queue_full\":1,\"maximum_command_batch\":%u,\"services\":%u}\n",
    Executor::command_budget, f.done.load(), owner_pass_received(), f.maximum, f.services);
}
static void stop_rejects_queued() {
  Fixture f; f.start();
  for (unsigned i = 0; i < Executor::command_capacity; ++i)
    CHECK(f.executor.post([&f](uint32_t) { ++f.done; }, [&f](wl_err_t error) {
      CHECK(f.executor.on_owner_thread() && error == WL_ERR_NOT_INITIALIZED); ++f.rejected;
    }) == WL_OK);
  f.executor.request_stop();
  CHECK(f.executor.post([](uint32_t) {}) == WL_ERR_NOT_INITIALIZED);
  f.release(); CHECK(f.executor.stop() == WL_OK);
  CHECK(f.done == 0 && f.rejected == Executor::command_capacity && f.stop_calls == 1);
  CHECK(f.executor.last_error() == WL_OK && wl_kernel_quiescent(f.core));
  std::printf("{\"format\":\"wirelink-owner-kernel-v1\",\"case\":\"kernel_stop_reject\",\"budget\":%u,"
    "\"delivered\":0,\"received\":0,\"rejected\":%u,\"stop_calls\":%u}\n",
    Executor::command_budget, f.rejected, f.stop_calls);
}
int main() { available_burst(); stop_rejects_queued(); }
