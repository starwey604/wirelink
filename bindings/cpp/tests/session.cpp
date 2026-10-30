/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/cpp/session.hpp"
#include "wirelink/asio/packet_udp.hpp"
#include "wirelink/host/executor.hpp"
#include "endpoint.h"
#include <cstdlib>
#include <atomic>
#include <cstdio>
#include <new>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
#define require(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); std::abort(); } } while (0)
static void *prepare(wl_host_endpoint_t &driver, wirelink::Error &error, bool echo) {
  void *storage = ::operator new(cpp_test_size(), std::align_val_t(cpp_test_alignment()));
  auto status = cpp_test_prepare(storage, &driver, echo);
  if (status != WL_OK) {
    ::operator delete(storage, std::align_val_t(cpp_test_alignment()));
    error = wirelink::Error::local(status); return nullptr;
  }
  return storage;
}
static void *create(wl_host_endpoint_t &driver, wirelink::Error &error) {
  return prepare(driver, error, false);
}
static void destroy(void *storage) noexcept {
  ::operator delete(storage, std::align_val_t(cpp_test_alignment()));
}
struct Call : wirelink::detail::OperationState<int32_t> {
  request_value_t request{};
  response_value_t response{};
  cpp_test_call_t bridge{};
  explicit Call(int32_t input) { request.has_input = true; request.input = input; }
  wl_err_t submit(uint32_t now, uint32_t timeout, wl_host_rpc_notify_fn notify,
                  void *user, wl_kernel_call_handle_t *handle) noexcept override {
    bridge.endpoint = endpoint; bridge.request = &request; bridge.response = &response;
    return cpp_test_submit(&bridge, now, timeout, notify, user, handle);
  }
  int32_t decode_response() override { require(response.has_output); return response.output; }
};
class Server {
public:
  wirelink::host::Executor owner;
  wl_host_endpoint_t driver{};
  void *storage{};
  std::unique_ptr<wirelink::asio::PacketUdpAdapter> adapter;
  explicit Server(bool echo) {
    auto error = wirelink::Error::local(WL_ERR_INVALID_STATE);
    storage = prepare(driver, error, echo); require(storage != nullptr);
    adapter = std::make_unique<wirelink::asio::PacketUdpAdapter>(*driver.port);
    static uint64_t instance = 81971;
    require(cpp_test_initialize(storage, instance++, wirelink::asio::PacketUdpAdapter::submit,
        wirelink::asio::PacketUdpAdapter::notify, adapter.get()) == WL_OK);
    wirelink::asio::PacketUdpConfig config;
    config.bind_address = "127.0.0.1"; config.maximum_datagram_size = driver.maximum_rx_frame;
    std::error_code os; require(adapter->start(config, os) == WL_OK);
  }
  void start(uint16_t peer) {
    require(adapter->set_peer("127.0.0.1", peer) == WL_OK);
    wirelink::host::ExecutorIo io;
    io.user = adapter.get();
    io.service = [](void *p, uint32_t) { return static_cast<wirelink::asio::PacketUdpAdapter *>(p)->service(); };
    io.readiness = [](void *p, uint32_t) {
      return wl_io_readiness_at(static_cast<wirelink::asio::PacketUdpAdapter *>(p)->ready() ? 0U : UINT32_MAX);
    };
    io.wait = [](void *p, uint32_t ms) {
      return static_cast<wirelink::asio::PacketUdpAdapter *>(p)->wait_for_activity(std::chrono::milliseconds(ms));
    };
    io.notify = wirelink::asio::PacketUdpAdapter::notify;
    io.stop = [](void *p) { static_cast<wirelink::asio::PacketUdpAdapter *>(p)->stop(); };
    require(owner.initialize(*driver.core, io) == WL_OK && owner.start() == WL_OK);
  }
  ~Server() {
    require(owner.stop() == WL_OK); adapter->stop(); adapter.reset();
    require(wl_kernel_quiescent(driver.core)); destroy(storage);
  }
};
static wirelink::Session connect(Server &server) {
  wirelink::UdpOptions options; options.peer_port = server.adapter->local_port();
  auto result = wirelink::Session::connect(options, {create, cpp_test_initialize, destroy});
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
    require(!refused->done() && refused->bridge.callbacks == 0 && !refused->keepalive);
    wirelink::Operation<int32_t> cancelled(calls.back());
    require(cancelled.cancel() && cancelled.wait_for(1s));
    require(!cancelled.result() && cancelled.result().error().kind == wirelink::ErrorKind::cancelled);
    server.start(client.local_port());
    for (unsigned i = 0; i < 3; ++i) {
      require(calls[i]->wait_for(2s)); auto result = calls[i]->result();
      require(bool(result) && result.value() == int32_t(i + 1));
      require(calls[i]->bridge.callbacks == 1);
    }
    require(calls.back()->bridge.callbacks == 1 && refused->bridge.callbacks == 0);
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
    require(closing->bridge.callbacks == 1 && timeout->bridge.callbacks == 1);
    require(client.submit(std::make_shared<Call>(3), 1000) == WL_ERR_NOT_INITIALIZED);
  }
  {
    struct Clock {
      std::atomic<uint32_t> value{UINT32_MAX - 15U};
      std::atomic<unsigned> reads{};
      uint64_t instance{984371};
    } clock;
    Server silent(false);
    wirelink::UdpOptions options; options.peer_port = silent.adapter->local_port();
    options.environment.clock = {[](void *p) -> uint32_t {
      auto *c = static_cast<Clock *>(p); ++c->reads; return c->value.load();
    }, &clock};
    options.environment.session = {[](void *p, uint64_t *out) -> wl_err_t {
      *out = static_cast<Clock *>(p)->instance++; return WL_OK;
    }, &clock};
    auto connection = wirelink::Session::connect(options, {create, cpp_test_initialize, destroy});
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
    require(call->bridge.callbacks == 1);
  }

}
