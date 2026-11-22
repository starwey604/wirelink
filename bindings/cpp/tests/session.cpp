/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/cpp/session.hpp"
#include "wirelink/udp.h"
#include "wirelink/host/executor.hpp"
#include "probe.h"
#include <cstdlib>
#include <atomic>
#include <cstdio>
#include <new>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
#define require(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); std::abort(); } } while (0)
static int32_t execute(void *, const request_t *request, response_t *response) {
  if (request->input == INT32_MIN) return WL_ERR_TIMEOUT;
  response->output = request->input;
  return 0;
}
static wl_err_t create(uint32_t instance, wl_transport_t *transport, wl_node_t **out) {
  auto owner = std::make_unique<probe_node_t>();
  probe_handlers_t handlers{};
  const auto status = probe_bind_instance(owner.get(), instance, transport, &handlers, nullptr);
  if (status == WL_OK) *out = &owner.release()->node;
  return status;
}
static void destroy(wl_node_t *node) noexcept {
  delete reinterpret_cast<probe_node_t *>(node);
}
struct Call : wirelink::detail::OperationState<int32_t> {
  request_t request{};
  response_t response{};
  unsigned callbacks{};
  wl_host_rpc_notify_fn completion{};
  void *callback_user{};
  explicit Call(int32_t input) { request.input = input; }
  wl_err_t submit(uint32_t now, uint32_t timeout, wl_host_rpc_notify_fn notify,
                  void *user, wl_call_handle_t *handle) noexcept override {
    completion = notify;
    callback_user = user;
    const auto done = [](void *context, wl_rpc_result_t result, const response_t *value) {
      auto &call = *static_cast<Call *>(context);
      ++call.callbacks;
      if (result.outcome == WL_RPC_SUCCESS) {
        require(value);
        call.response = *value;
      }
      call.completion(call.callback_user, result);
    };
    return probe_call_execute_ex(static_cast<probe_node_t *>(endpoint), &request,
                                 now, timeout, done, this, handle);
  }
  int32_t decode_response() override { return response.output; }
};
class Server {
public:
  wirelink::host::Executor owner;
  probe_node_t node{};
  wl_udp_t *udp{};
  explicit Server(bool echo) {
    wl_udp_config_t config{};
    config.bind_address = "127.0.0.1";
    require(wl_udp_open(&udp, &config, nullptr) == WL_OK);
    probe_handlers_t handlers{};
    handlers.on_execute = echo ? execute : nullptr;
    require(probe_bind(&node, wl_udp_transport(udp), &handlers, this) == WL_OK);
  }
  void start(uint16_t peer) {
    require(wl_udp_set_peer(udp, "127.0.0.1", peer) == WL_OK);
    require(owner.initialize(node.node) == WL_OK && owner.start() == WL_OK);
  }
  ~Server() {
    require(owner.stop() == WL_OK);
    require(wl_node_close(&node.node, 0) == WL_OK);
    wl_udp_close(udp);
  }
};
static wirelink::Session connect(Server &server) {
  wirelink::UdpOptions options; options.peer_port = wl_udp_local_port(server.udp);
  auto result = wirelink::Session::connect(options, {create, destroy});
  require(bool(result)); return std::move(result).value();
}
int main() {
  {
    Server server(true);
    auto client = connect(server);
    std::vector<std::shared_ptr<Call>> calls;
    for (int32_t i = 1; i <= 4; ++i) {
      auto call = std::make_shared<Call>(i);
      require(client.submit(call, 5000) == WL_OK);
      call->request.input = 999; // Core snapshot must already exist at successful return.
      calls.push_back(std::move(call));
    }
    auto refused = std::make_shared<Call>(8);
    require(client.submit(refused, 5000) == WL_ERR_WOULD_BLOCK);
    require(!refused->done() && refused->callbacks == 0 && !refused->keepalive);
    wirelink::Operation<int32_t> cancelled(calls.back());
    require(cancelled.cancel() && cancelled.wait_for(1s));
    require(!cancelled.result() && cancelled.result().error().kind == wirelink::ErrorKind::cancelled);
    server.start(client.local_port());
    for (unsigned i = 0; i < 3; ++i) {
      require(calls[i]->wait_for(2s)); auto result = calls[i]->result();
      require(bool(result) && result.value() == int32_t(i + 1));
      require(calls[i]->callbacks == 1);
    }
    require(calls.back()->callbacks == 1 && refused->callbacks == 0);
    auto rejected = std::make_shared<Call>(INT32_MIN);
    require(client.submit(rejected, 1000) == WL_OK && rejected->wait_for(2s));
    auto result = rejected->result();
    require(!result && result.error().kind == wirelink::ErrorKind::rejected &&
        result.error().completion.status == WL_ERR_TIMEOUT);
    require(client.submit(rejected, 1000) == WL_ERR_INVALID_STATE);
    client.close();
    require(calls[0]->result().value() == 1 && !client.is_open());
  }
  {
    Server silent(false); auto client = connect(silent); silent.start(client.local_port());
    auto timeout = std::make_shared<Call>(1);
    require(client.submit(timeout, 30) == WL_OK && timeout->wait_for(1s));
    require(!timeout->result() && timeout->result().error().kind == wirelink::ErrorKind::timed_out);
    auto closing = std::make_shared<Call>(2);
    require(client.submit(closing, 10000) == WL_OK);
    auto discarded = std::make_shared<Call>(7);
    std::weak_ptr<Call> retained = discarded;
    require(client.submit(discarded, 10000) == WL_OK);
    discarded.reset(); require(!retained.expired());
    std::thread a([&] { client.close(); }), b([&] { client.close(); });
    a.join(); b.join();
    require(retained.expired());
    require(closing->wait_for(1s) && closing->result().error().kind == wirelink::ErrorKind::closed);
    require(closing->callbacks == 1 && timeout->callbacks == 1);
    require(client.submit(std::make_shared<Call>(3), 1000) == WL_ERR_NOT_INITIALIZED);
  }
  {
    struct Clock {
      std::atomic<uint32_t> value{UINT32_MAX - 15U};
      std::atomic<unsigned> reads{};
      uint32_t instance{984371};
    } clock;
    Server silent(false);
    wirelink::UdpOptions options; options.peer_port = wl_udp_local_port(silent.udp);
    options.environment.clock = {[](void *p) -> uint32_t {
      auto *c = static_cast<Clock *>(p); ++c->reads; return c->value.load();
    }, &clock};
    options.environment.session = {[](void *p, uint32_t *out) -> wl_err_t {
      *out = static_cast<Clock *>(p)->instance++; return WL_OK;
    }, &clock};
    auto connection = wirelink::Session::connect(options, {create, destroy});
    require(bool(connection)); auto client = std::move(connection).value();
    silent.start(client.local_port());
    auto call = std::make_shared<Call>(1);
    require(client.submit(call, 10000) == WL_OK);
    std::this_thread::sleep_for(20ms);
    unsigned idle = clock.reads;
    std::this_thread::sleep_for(20ms);
    require(clock.reads.load() - idle <= 4 && !call->done());
    clock.value = uint32_t((UINT32_MAX - 15U) + uint32_t(10000));
    client.notify(); // Would otherwise remain in a real ten-second wait.
    require(call->wait_for(500ms) && call->result().error().kind == wirelink::ErrorKind::timed_out);
    client.close();
    require(call->callbacks == 1);
  }

}
