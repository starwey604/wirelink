/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/host/executor.hpp"

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
struct Context : wl_transport_t {
  wl_node_t node{};
  uint8_t rx[2][64]{}, tx[WL_ENDPOINT_TX_COUNT][64]{};
  wl_transport_ops_t operations{};
  Executor owner;
  std::atomic<unsigned> completed{}, services{}, commands{}, rejected{};
  std::atomic<bool> flood{true};
};
static void bind(Context &context, uint32_t instance) {
  context.ops = &context.operations;
  context.operations.attach = [](wl_transport_t *, const wl_transport_attach_t *) -> wl_err_t { return WL_OK; };
  context.operations.submit = [](wl_transport_t *, const uint8_t *, size_t, wl_packet_token_t) -> wl_packet_submit_t {
    return WL_PACKET_BUSY;
  };
  wl_node_layout_t layout{};
  layout.instance = instance;
  layout.storage = wl_endpoint_storage_uniform(&context.tx[0][0], sizeof(context.tx[0]));
  layout.rx_storage = &context.rx[0][0];
  layout.rx_storage_size = sizeof(context.rx);
  layout.rx_wire_max = 63;
  layout.tx_wire_max = 64;
  layout.has_tx = 1;
  require(wl_node_bind_transport(&context.node, &layout, &context) == WL_OK);
}
static void callback_marker() {}
static wl_err_t start_call(Context &context, uint32_t now, wl_endpoint_complete_fn complete) {
  auto *core = &context.node.core;
  uint8_t slot, *payload;
  size_t capacity;
  auto error = wl_endpoint_call_claim(core, &slot, &payload, &capacity);
  if (error != WL_OK) return error;
  require(capacity >= 1);
  payload[0] = 77;
  return wl_endpoint_call_commit(core, slot, 40, 41, 1, now, 20, 1, complete,
                                 callback_marker, &context, nullptr);
}
static void complete(wl_endpoint_t *, const wl_endpoint_call_t *call, wl_rpc_result_t result,
                       const uint8_t *, size_t) {
  auto &context = *static_cast<Context *>(call->user);
  require(context.owner.on_owner_thread() && result.outcome == WL_RPC_TIMED_OUT);
  ++context.completed;
}
int main() {
  Context context;
  auto &io = context.operations;
  io.notify = [](wl_transport_t *self) { static_cast<Context *>(self)->owner.notify(); };
  io.service = [](wl_transport_t *user, uint32_t) {
    ++static_cast<Context *>(user)->services;
    return wl_err_t(WL_ERR_BAD_FRAME); // Nonfatal ingress errors cannot starve deadlines.
  };
  io.readiness = [](const wl_transport_t *user, uint32_t) {
    return wl_io_readiness_at(static_cast<const Context *>(user)->flood ? 0U : UINT32_MAX);
  };
  bind(context, 901);
  require(context.owner.initialize(context.node) == WL_OK);
  require(context.owner.start() == WL_OK);
  std::promise<void> admitted;
  require(context.owner.post([&](uint32_t now) {
    for (unsigned i = 0; i < 4; ++i)
      require(start_call(context, now, complete) == WL_OK);
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
  require(wl_node_quiescent(&context.node));
  Context exceptional;
  exceptional.operations.wait = [](wl_transport_t *, uint32_t) -> wl_err_t { throw std::bad_alloc(); };
  exceptional.operations.notify = [](wl_transport_t *) {};
  bind(exceptional, 902);
  require(exceptional.owner.initialize(exceptional.node) == WL_OK);
  require(exceptional.owner.start() == WL_OK);
  until = std::chrono::steady_clock::now() + 1s;
  while (exceptional.owner.state() != Executor::State::stopped &&
         std::chrono::steady_clock::now() < until) std::this_thread::yield();
  require(exceptional.owner.state() == Executor::State::stopped);
  require(exceptional.owner.last_error() == WL_ERR_NO_MEM);
  require(exceptional.owner.stop() == WL_OK && wl_node_quiescent(&exceptional.node));

}
