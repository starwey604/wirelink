/* SPDX-License-Identifier: Apache-2.0 */
#include <calculator/client.hpp>
#include <wirelink/host/executor.hpp>
#include "server.hpp"
#include <future>
#include <vector>

using namespace std::chrono_literals;
using wirelink::ErrorKind;

static void values_concurrency_and_lifetime() {
  Server server;
  auto connect = [&] {
    auto opened = calculator::Client::connect({.peer_port = server.port()});
    CHECK(opened);
    return std::move(opened).value();
  };
  auto client = connect();
  for (int batch = 0; batch < 50; ++batch) {
    std::vector<wirelink::Operation<calculator::AddResponse>> calls;
    for (int i = 0; i < 4; ++i) {
      calculator::AddRequest request{i, 42 - i};
      auto admitted = client.add_async(request, 5s);
      CHECK(admitted);
      calls.push_back(std::move(admitted).value());
      request.left = -100; // Request storage is independent after admission.
    }
    for (const auto& call : calls) {
      CHECK(call.wait_for(5s));
      const auto result = call.result();
      CHECK(result && result.value().sum == 42 && !call.cancel());
      auto signal = std::make_shared<wirelink::CompletionSignal>();
      call.notify_on_completion(signal); // Late registration must still wake.
      CHECK(signal->wait());
    }
  }
  auto rejected = client.add_async({INT32_MAX, 1});
  CHECK(rejected && rejected.value().result().error().kind == ErrorKind::rejected);
  CHECK(client.add_async({1, 2}, 0ms).error().kind == ErrorKind::invalid_argument);
  CHECK(client.add_async({1, 2}, std::chrono::milliseconds(INT64_MAX)).error().kind == ErrorKind::invalid_argument);
  // Both abandoned handles and retained operations are safe at client teardown.
  auto survives = [&] {
    Server other_peer;
    auto other = calculator::Client::connect({.peer_port = other_peer.port()});
    CHECK(other);
    auto temporary = std::move(other).value();
    auto call = temporary.add_async({20, 22});
    CHECK(call && call.value().wait_for(2s));
    return std::move(call).value();
  }();
  CHECK(survives.result().value().sum == 42 && !survives.cancel());
  for (int i = 0; i < 4; ++i) CHECK(client.add_async({1, 2}, 5s));
  auto moved = std::move(client);
  CHECK(client.add_async({1, 2}).error().kind == ErrorKind::closed);
  moved.close();
}

static void cancellation_timeout_and_close() {
  Server server;
  auto opened = calculator::Client::connect({.peer_port = server.port()});
  CHECK(opened);
  auto client = std::move(opened).value();
  server.block();
  auto first = client.add_async({1, 2}, 60s);
  CHECK(first);
  server.wait_entered();
  auto second = client.add_async({20, 22}, 60s);
  CHECK(second);
  CHECK(!first.value().wait_for(1ms));
  CHECK(first.value().cancel());
  CHECK(first.value().wait_for(2s));
  CHECK(first.value().result().error().kind == ErrorKind::cancelled);
  CHECK(!first.value().cancel());
  CHECK(!second.value().done());
  server.unblock();
  CHECK(second.value().wait_for(3s) && second.value().result().value().sum == 42);

  server.block();
  auto timeout = client.add_async({1, 2}, 100ms);
  CHECK(timeout);
  server.wait_entered();
  CHECK(timeout.value().wait_for(2s));
  CHECK(timeout.value().result().error().kind == ErrorKind::timed_out);
  server.unblock();
  CHECK(client.add_async({1, 2}, 2s).value().result().value().sum == 3);

  server.block();
  std::vector<wirelink::Operation<calculator::AddResponse>> calls;
  for (int i = 0; i < 4; ++i) {
    auto call = client.add_async({i, 2}, 60s);
    CHECK(call);
    calls.push_back(std::move(call).value());
  }
  server.wait_entered();
  auto a = std::async(std::launch::async, [&] { client.close(); });
  auto b = std::async(std::launch::async, [&] { client.close(); });
  CHECK(a.wait_for(2s) == std::future_status::ready);
  CHECK(b.wait_for(2s) == std::future_status::ready);
  for (auto& call : calls) {
    CHECK(call.done());
    CHECK(call.result().error().kind == ErrorKind::closed);
  }
  CHECK(client.add_async({1, 2}).error().kind == ErrorKind::closed);
  server.unblock();
}

static void destruction_finishes_pending() {
  Server server;
  server.block();
  auto operation = [&] {
    auto opened = calculator::Client::connect({.peer_port = server.port()});
    CHECK(opened);
    auto client = std::move(opened).value();
    auto call = client.add_async({1, 2}, 60s);
    CHECK(call);
    server.wait_entered();
    return std::move(call).value();
  }();
  CHECK(operation.done());
  CHECK(operation.result().error().kind == ErrorKind::closed);
  CHECK(!operation.cancel());
  server.unblock();
}

static void response_cancel_close_races() {
  for (int i = 0; i < 80; ++i) {
    Server server;
    auto opened = calculator::Client::connect({.peer_port = server.port()});
    CHECK(opened);
    auto client = std::move(opened).value();
    auto admitted = client.add_async({20, 22}, 2s);
    CHECK(admitted);
    auto operation = std::move(admitted).value();
    auto cancel = std::async(std::launch::async, [&] { (void)operation.cancel(); });
    if (i % 2 == 0) client.close();
    cancel.get();
    CHECK(operation.wait_for(3s));
    auto result = operation.result();
    CHECK(result ? result.value().sum == 42 : (result.error().kind == ErrorKind::cancelled || result.error().kind == ErrorKind::closed));
    client.close();
    CHECK(operation.done() && !operation.cancel());
  }
}

int main() {
  values_concurrency_and_lifetime();
  cancellation_timeout_and_close();
  destruction_finishes_pending();
  response_cancel_close_races();
  std::puts("native async ownership, concurrency, cancellation, deadlines and shutdown PASS");
}
