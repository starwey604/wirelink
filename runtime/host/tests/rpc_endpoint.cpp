/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_endpoint.h"
#include "wirelink/asio/udp_adapter.hpp"
#include "wirelink/host/executor.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <asio.hpp>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::abort(); } } while (0)
using namespace std::chrono_literals;
using wirelink::host::Executor;
using wirelink::asio::UdpAdapter;
static std::atomic<unsigned> handlers{};
static std::atomic<wl_time_ms_t> manual_time{UINT32_MAX - 5U};

template<class Predicate> static void eventually(Predicate ready) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!ready()) {
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(1ms); // Test harness only, never executor waiting.
  }
}
struct Gate {
  wl_endpoint_driver_t generated;
  std::mutex mutex;
  std::condition_variable cv;
  bool blocked{}, entered{};
  std::atomic<unsigned> passes{};
  explicit Gate(wl_endpoint_driver_t value) : generated(value) {}
  wl_endpoint_driver_t driver() {
    return {generated.endpoint, this, [](void* p) -> wl_err_t {
      auto& self = *static_cast<Gate*>(p);
      {
        std::unique_lock lock(self.mutex);
        if (self.blocked) {
          self.entered = true;
          self.cv.notify_all();
          self.cv.wait(lock, [&self] { return !self.blocked; });
        }
      }
      ++self.passes;
      return self.generated.step(self.generated.context);
    }, [](void* p) -> wl_err_t {
      auto& self = *static_cast<Gate*>(p);
      return self.generated.close(self.generated.context);
    }};
  }
  void hold(Executor& executor) {
    std::unique_lock lock(mutex);
    blocked = true; entered = false;
    executor.notify();
    CHECK(cv.wait_for(lock, 5s, [this] { return entered; }));
  }
  void release() {
    std::lock_guard lock(mutex);
    blocked = false;
    cv.notify_all();
  }
};
static int32_t add(void* p, const add_request_value_t* request, add_response_value_t* response) {
  ++handlers;
  add_response_value_t ignored;
  const auto nested = calculator_endpoint_add_sync(static_cast<calculator_endpoint_t*>(p), request, &ignored, 10);
  CHECK(nested.status == WL_RPC_FAILED && nested.local_error == WL_ERR_REENTRANT);
  if (request->left < 0) return 17;
  response->has_sum = true;
  response->sum = request->left + request->right;
  return 0;
}

static void active_call_shutdown(bool fail_wait) {
  Executor executor;
  calculator_endpoint_t endpoint{};
  CHECK(calculator_endpoint_init(&endpoint, 91, wirelink::host::monotonic_clock()) == WL_OK);
  wirelink::asio::UdpAdapterConfig config;
  config.bind_address = "127.0.0.1";
  std::error_code error;
  auto udp = UdpAdapter::open(*calculator_endpoint_handle(&endpoint), config, error);
  CHECK(udp && !error);
  ::asio::io_context io;
  ::asio::ip::udp::socket blackhole(io, {::asio::ip::udp::v4(), 0});
  CHECK(udp->set_peer("127.0.0.1", blackhole.local_endpoint().port()) == WL_OK);
  struct Wait {
    wl_waiter_t underlying;
    std::atomic<bool> entered{}, fail{};
  } wait{*wl_endpoint_waiter(calculator_endpoint_handle(&endpoint))};
  const wl_waiter_t descriptor{
    [](void* p, uint32_t maximum) -> wl_err_t {
      auto& self = *static_cast<Wait*>(p);
      self.entered = true;
      const auto result = self.underlying.wait(self.underlying.user_data, maximum);
      return self.fail.load() ? WL_ERR_IO : result;
    }, &wait,
    [](void* p) { auto& self = *static_cast<Wait*>(p); self.underlying.notify(self.underlying.user_data); }
  };
  CHECK(wl_endpoint_set_waiter(calculator_endpoint_handle(&endpoint), &descriptor) == WL_OK);
  CHECK(executor.initialize(calculator_endpoint_driver(&endpoint)) == WL_OK);
  CHECK(executor.start() == WL_OK);
  std::thread caller([&] {
    add_request_value_t request{};
    request.has_left = request.has_right = true;
    add_response_value_t response{};
    response.sum = 123;
    const auto result = calculator_endpoint_add_sync(&endpoint, &request, &response, 1000000);
    CHECK(result.status == (fail_wait ? WL_RPC_FAILED : WL_RPC_CANCELLED));
    if (fail_wait) CHECK(result.local_error == WL_ERR_IO);
    CHECK(response.sum == 123);
  });
  // Receipt proves the owner submitted this job, not just the producer queue.
  blackhole.non_blocking(true);
  std::array<uint8_t, 128> bytes{};
  ::asio::ip::udp::endpoint source;
  eventually([&] {
    std::error_code received;
    return blackhole.receive_from(::asio::buffer(bytes), source, 0, received) > 0 && !received;
  });
  eventually([&] { return wait.entered.load(); });
  if (fail_wait) { wait.fail = true; executor.notify(); }
  else executor.requestStop();
  caller.join();
  executor.stop();
  CHECK(executor.stats().m_rpc_completed == 1);
}

int main() {
  Executor client_executor, server_executor;
  calculator_endpoint_t client{}, server{};
  const wl_clock_t clock{[](void*) { return manual_time.load(); }, nullptr};
  CHECK(calculator_endpoint_init(&client, 71, clock) == WL_OK);
  calculator_endpoint_config_t config;
  CHECK(calculator_endpoint_config_defaults(&config, 72) == WL_OK);
  config.clock = clock; config.on_add = add; config.add_user_data = &server;
  CHECK(calculator_endpoint_init_config(&server, &config) == WL_OK);
  wirelink::asio::UdpAdapterConfig udp_config;
  udp_config.bind_address = "127.0.0.1";
  std::error_code error;
  auto a = UdpAdapter::open(*calculator_endpoint_handle(&client), udp_config, error);
  auto b = UdpAdapter::open(*calculator_endpoint_handle(&server), udp_config, error);
  CHECK(a && b && !error);
  CHECK(a->set_peer("127.0.0.1", b->local_port()) == WL_OK);
  CHECK(b->set_peer("127.0.0.1", a->local_port()) == WL_OK);
  Gate gate{calculator_endpoint_driver(&client)};
  CHECK(client_executor.initialize(gate.driver()) == WL_OK);
  CHECK(server_executor.initialize(calculator_endpoint_driver(&server)) == WL_OK);
  CHECK(client_executor.start() == WL_OK && server_executor.start() == WL_OK);
  add_request_value_t request{};
  request.has_left = request.has_right = true; request.left = 20; request.right = 22;
  add_response_value_t response{};
  for (unsigned i = 0; i < 100; ++i) {
    const auto result = calculator_endpoint_add_sync(&client, &request, &response, 1500);
    CHECK(result.status == WL_RPC_SUCCESS && response.sum == 42 && result.local_error == WL_OK);
  }
  request.left = -1;
  const auto rejected = calculator_endpoint_add_sync(&client, &request, &response, 100);
  CHECK(rejected.status == WL_RPC_REJECTED && rejected.rejection == 17 && response.sum == 42);
  request.left = 20;
  std::array<std::thread, 4> callers;
  for (auto& caller : callers) caller = std::thread([&] {
    add_response_value_t out;
    for (unsigned i = 0; i < 25; ++i) {
      const auto result = calculator_endpoint_add_sync(&client, &request, &out, 1500);
      CHECK(result.status == WL_RPC_SUCCESS && out.sum == 42);
    }
  });
  for (auto& caller : callers) caller.join();
  CHECK(handlers == 201);
  std::this_thread::sleep_for(50ms);
  const auto passes = gate.passes.load();
  std::this_thread::sleep_for(50ms);
  CHECK(gate.passes.load() - passes <= 2); // No periodic 1-ms executor/UDP polling.

  // Hold the owner before admission dispatch, fill the bounded proxy queue,
  // advance its SAME clock across wrap, then release: expired work is not sent.
  gate.hold(client_executor);
  const auto submitted = client_executor.stats().m_rpc_submitted;
  std::array<std::thread, 8> queued;
  for (auto& caller : queued) caller = std::thread([&] {
    add_response_value_t out{};
    out.sum = 99;
    const auto result = calculator_endpoint_add_sync(&client, &request, &out, 10);
    CHECK(result.status == WL_RPC_TIMED_OUT && result.local_error == WL_OK && out.sum == 99);
  });
  eventually([&] { return client_executor.stats().m_rpc_submitted == submitted + queued.size(); });
  const auto full = calculator_endpoint_add_sync(&client, &request, &response, 10);
  CHECK(full.status == WL_RPC_FAILED && full.local_error == WL_ERR_BUSY);
  manual_time.fetch_add(11);
  gate.release();
  for (auto& caller : queued) caller.join();
  CHECK(handlers == 201 && client_executor.stats().m_rpc_queue_full == 1);

  // Stop wakes queued callers without timeout polling or dangling stack jobs.
  gate.hold(client_executor);
  const auto before_stop = client_executor.stats().m_rpc_submitted;
  std::thread blocked([&] {
    add_response_value_t out;
    const auto result = calculator_endpoint_add_sync(&client, &request, &out, 1000000);
    CHECK(result.status == WL_RPC_CANCELLED);
  });
  eventually([&] { return client_executor.stats().m_rpc_submitted == before_stop + 1; });
  const uint8_t latest[] = {1};
  CHECK(client_executor.submitLatest(97, latest, sizeof(latest)) == WL_OK);
  client_executor.requestStop();
  gate.release();
  client_executor.stop();
  blocked.join();
  server_executor.stop();
  CHECK(client_executor.stats().m_rpc_submitted == client_executor.stats().m_rpc_completed);
  CHECK(client_executor.stats().m_latest_cancelled == 1);
  CHECK(calculator_endpoint_add_sync(&client, &request, &response, 10).status == WL_RPC_CANCELLED);
  CHECK(response.sum == 42);
  active_call_shutdown(false);
  active_call_shutdown(true);
  std::puts("RPC executor: typed proxy/concurrency/queue deadlines/wrap/stop/idle PASS");
}
