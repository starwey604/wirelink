/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/host/executor.hpp"
#include "owner.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <future>
#include <new>
#include <semaphore>
#include <vector>
using namespace std::chrono_literals;
using wirelink::host::Executor;
#define require(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); std::abort(); } } while (0)
struct Context {
  Executor owner;
  std::atomic<unsigned> completed{}, services{}, commands{}, rejected{};
  std::atomic<bool> flood{true};
};
static wl_packet_submit_t busy(void *, const uint8_t *, size_t, wl_packet_token_t) {
  return WL_PACKET_BUSY;
}
static void notify(void *user) { static_cast<Context *>(user)->owner.notify(); }
static void complete(wl_kernel_t *, const wl_kernel_call_t *call, wl_kernel_result_t result,
                       const uint8_t *, size_t) {
  auto &context = *static_cast<Context *>(call->user);
  require(context.owner.on_owner_thread() && result.outcome == WL_KERNEL_TIMED_OUT);
  ++context.completed;
}
int main() {
  Context context;
  require(executor_test_init(901, busy, notify, &context) == WL_OK);
  wirelink::host::ExecutorIo io;
  io.user = &context;
  io.service = [](void *user, uint32_t) {
    ++static_cast<Context *>(user)->services;
    return wl_err_t(WL_ERR_BAD_FRAME); // Nonfatal ingress errors cannot starve deadlines.
  };
  io.readiness = [](void *user, uint32_t) {
    return wl_io_readiness_at(static_cast<Context *>(user)->flood ? 0U : UINT32_MAX);
  };
  require(context.owner.initialize(*executor_test_core(), io) == WL_OK);
  require(context.owner.start() == WL_OK);
  std::promise<void> admitted;
  require(context.owner.post([&](uint32_t now) {
    for (unsigned i = 0; i < 4; ++i)
      require(executor_test_call(now, 20, complete, &context) == WL_OK);
    require(context.owner.stop() == WL_ERR_REENTRANT);
    admitted.set_value();
  }) == WL_OK);
  require(admitted.get_future().wait_for(1s) == std::future_status::ready);
  auto until = std::chrono::steady_clock::now() + 1s;
  while (context.completed != 4 && std::chrono::steady_clock::now() < until)
    std::this_thread::yield();
  require(context.completed == 4);
  context.flood = false;
  context.owner.notify();
  std::this_thread::sleep_for(20ms);
  auto idle = context.services.load();
  std::this_thread::sleep_for(20ms);
  require(context.services.load() - idle <= 2); // No periodic polling/self-generated wake loop.

  std::vector<std::thread> producers;
  for (unsigned i = 0; i < 4; ++i) producers.emplace_back([&] {
    for (unsigned n = 0; n < 100; ++n) {
      wl_err_t result;
      do {
        result = context.owner.post([&](uint32_t) {
          require(context.owner.on_owner_thread()); ++context.commands;
        });
        if (result == WL_ERR_QUEUE_FULL) std::this_thread::yield();
      } while (result == WL_ERR_QUEUE_FULL);
      require(result == WL_OK);
    }
  });
  for (auto &producer : producers) producer.join();
  until = std::chrono::steady_clock::now() + 1s;
  while (context.commands != 400 && std::chrono::steady_clock::now() < until)
    std::this_thread::yield();
  require(context.commands == 400);

  std::binary_semaphore entered(0), release(0);
  // Controlled fixture pause makes queue pressure/shutdown ordering deterministic.
  require(context.owner.post([&](uint32_t) { entered.release(); release.acquire(); }) == WL_OK);
  entered.acquire();
  for (unsigned n = 0; n < Executor::command_capacity; ++n)
    require(context.owner.post([&](uint32_t) { std::abort(); }, [&](wl_err_t error) {
      require(context.owner.on_owner_thread() && error == WL_ERR_NOT_INITIALIZED);
      ++context.rejected;
    }) == WL_OK);
  require(context.owner.post([](uint32_t) {}) == WL_ERR_QUEUE_FULL);
  context.owner.request_stop();
  release.release();
  std::thread closer([&] { require(context.owner.stop() == WL_OK); });
  require(context.owner.stop() == WL_OK);
  closer.join();
  require(context.rejected == Executor::command_capacity);
  require(context.owner.post([](uint32_t) {}) == WL_ERR_NOT_INITIALIZED);
  require(wl_kernel_quiescent(executor_test_core()));
  Context exceptional;
  require(executor_test_init(902, busy, notify, &exceptional) == WL_OK);
  wirelink::host::ExecutorIo failed_wait;
  failed_wait.wait = [](void *, uint32_t) -> wl_err_t { throw std::bad_alloc(); };
  failed_wait.notify = [](void *) {};
  require(exceptional.owner.initialize(*executor_test_core(), failed_wait) == WL_OK);
  require(exceptional.owner.start() == WL_OK);
  until = std::chrono::steady_clock::now() + 1s;
  while (exceptional.owner.state() != Executor::State::stopped &&
         std::chrono::steady_clock::now() < until) std::this_thread::yield();
  require(exceptional.owner.state() == Executor::State::stopped);
  require(exceptional.owner.last_error() == WL_ERR_NO_MEM);
  require(exceptional.owner.stop() == WL_OK && wl_kernel_quiescent(executor_test_core()));

}
