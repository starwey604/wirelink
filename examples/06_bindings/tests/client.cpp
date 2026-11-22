/* SPDX-License-Identifier: Apache-2.0 */
#include <calculator/client.hpp>
#include "server.hpp"
#include <future>
#include <vector>

using namespace std::chrono_literals;
using wirelink::ErrorKind;

static void values_and_move() {
  Server server;
  auto opened = calculator::Client::connect({.peer_port = server.port()});
  CHECK(opened);
  auto client = std::move(opened).value();
  CHECK(!opened.value().is_open());
  CHECK(opened.value().add({1, 2}).error().kind == ErrorKind::closed);
  CHECK(client.is_open() && client.local_port() != 0);
  for (int i = 0; i < 100; ++i) {
    auto result = client.add({i, 42 - i});
    CHECK(result && result.value().sum == 42);
  }
  auto rejected = client.add({INT32_MAX, 1});
  CHECK(!rejected && rejected.error().kind == ErrorKind::rejected);
  CHECK(rejected.error().completion.status == 1);
  CHECK(client.add({0, 0}, 0ms).error().kind == ErrorKind::invalid_argument);
  CHECK(client.add({0, 0}, std::chrono::milliseconds(INT64_MAX)).error().kind == ErrorKind::invalid_argument);
  auto saved = client.add({-10, 52});
  client.close();
  client.close();
  CHECK(!client.is_open() && saved.value().sum == 42);
  CHECK(client.add({1, 2}).error().kind == ErrorKind::closed);
}

static void concurrent_calls() {
  Server server;
  auto opened = calculator::Client::connect({.peer_port = server.port()});
  CHECK(opened);
  auto client = std::move(opened).value();
  std::vector<std::thread> callers;
  for (int i = 0; i < 4; ++i) callers.emplace_back([&, i] {
    for (int n = 0; n < 40; ++n) {
      auto result = client.add({i, n}, 5s);
      CHECK(result && result.value().sum == i + n);
    }
  });
  for (auto& caller : callers) caller.join();
}

static void close_pending() {
  Server server;
  auto opened = calculator::Client::connect({.peer_port = server.port()});
  CHECK(opened);
  auto client = std::move(opened).value();
  server.block();
  auto caller = std::async(std::launch::async, [&] { return client.add({1, 2}, 60s); });
  server.wait_entered(); // Receipt proves admission; no timing guess.
  auto close_a = std::async(std::launch::async, [&] { client.close(); });
  auto close_b = std::async(std::launch::async, [&] { client.close(); });
  CHECK(close_a.wait_for(2s) == std::future_status::ready);
  CHECK(close_b.wait_for(2s) == std::future_status::ready);
  const auto result = caller.get();
  CHECK(!result && result.error().kind == ErrorKind::closed);
  server.unblock();
}

static void timeout_and_reuse() {
  Server server;
  auto opened = calculator::Client::connect({.peer_port = server.port()});
  CHECK(opened);
  auto client = std::move(opened).value();
  server.block();
  auto caller = std::async(std::launch::async, [&] { return client.add({1, 2}, 100ms); });
  server.wait_entered();
  const auto result = caller.get();
  CHECK(!result && result.error().kind == ErrorKind::timed_out);
  server.unblock();
  auto next = client.add({20, 22}, 2s);
  CHECK(next && next.value().sum == 42);
}

static void rollback() {
  static unsigned create_calls;
  const wirelink::EndpointFactory failing{
    [](uint32_t, wl_transport_t *, wl_node_t **) -> wl_err_t { ++create_calls; return WL_ERR_NO_MEM; },
    [](wl_node_t *) noexcept { std::abort(); }
  };
  CHECK(!wirelink::Session::connect({.peer_port=1,.bind_address="invalid"}, failing));
  CHECK(!wirelink::Session::connect({.peer_address="invalid",.peer_port=1}, failing));
  Server server;
  auto collision = wirelink::Session::connect({.peer_port=1,.bind_port=server.port()}, failing);
  CHECK(!collision && collision.error().kind == ErrorKind::transport && collision.error().system_error);
  CHECK(create_calls == 0);
  auto failed = wirelink::Session::connect({.peer_port=1}, failing);
  CHECK(!failed && failed.error().kind == ErrorKind::out_of_memory && create_calls == 1);
}

int main() {
  rollback();
  values_and_move();
  concurrent_calls();
  close_pending();
  timeout_and_reuse();
  std::puts("calculator C++ ownership, RPC, concurrency and rollback PASS");
}
