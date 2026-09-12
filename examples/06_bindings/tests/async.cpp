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
    CHECK(call.result().error().kind == ErrorKind::cancelled);
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
  CHECK(operation.result().error().kind == ErrorKind::cancelled);
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
    CHECK(result ? result.value().sum == 42 : result.error().kind == ErrorKind::cancelled);
    client.close();
    CHECK(operation.done() && !operation.cancel());
  }
}

// Hold the owner at a known safe point to test queue admission, deadline and
// queued cancellation deterministically, without relying on scheduler timing.
struct HeldOwner {
  wirelink::host::Executor executor; // Outlives endpoint binding.
  calculator_endpoint_t endpoint{};
  std::atomic<wl_time_ms_t> now{100};
  std::mutex mutex;
  std::condition_variable changed;
  bool entered{}, released{};
  HeldOwner() {
    auto environment = wl_platform_environment();
    environment.clock = {[](void* context) -> wl_time_ms_t {
      return static_cast<HeldOwner*>(context)->now.load();
    }, this};
    CHECK(calculator_endpoint_init(&endpoint, environment) == WL_OK);
    auto driver = calculator_endpoint_driver(&endpoint);
    driver.context = this;
    driver.step = [](void* context) -> wl_err_t {
      auto& self = *static_cast<HeldOwner*>(context);
      {
        std::unique_lock lock(self.mutex);
        self.entered = true;
        self.changed.notify_all();
        self.changed.wait(lock, [&] { return self.released; });
      }
      return calculator_endpoint_step(&self.endpoint);
    };
    driver.close = [](void* context) -> wl_err_t {
      return calculator_endpoint_close(&static_cast<HeldOwner*>(context)->endpoint);
    };
    CHECK(executor.initialize(driver) == WL_OK && executor.start() == WL_OK);
    std::unique_lock lock(mutex);
    CHECK(changed.wait_for(lock, 3s, [&] { return entered; }));
  }
  void release() {
    std::lock_guard lock(mutex);
    released = true;
    changed.notify_all();
    executor.notify();
  }
  ~HeldOwner() { release(); executor.stop(); }
};

struct Probe final : wirelink::host::RpcTask {
  HeldOwner& owner;
  std::atomic<int> submissions{}, finishes{};
  wl_rpc_completion_t result{};
  std::thread::id thread;
  std::promise<void> completed;
  explicit Probe(HeldOwner& value) : owner(value) {}
  wl_err_t submit(wl_time_ms_t deadline, wl_rpc_sync_notify_fn, void*, wl_rpc_call_t*) noexcept override {
    ++submissions;
    const auto remaining = deadline - owner.now.load();
    return remaining == 0 || remaining > INT32_MAX ? WL_ERR_TIMEOUT : WL_ERR_INVALID_ARG;
  }
  wl_err_t cancel(const wl_rpc_call_t&) noexcept override { std::abort(); }
  void finish(const wl_rpc_completion_t& value) noexcept override {
    result = value;
    thread = std::this_thread::get_id();
    CHECK(++finishes == 1);
    completed.set_value();
  }
};

static void bounded_queue_and_queued_deadline() {
  for (bool shutdown : {false, true}) {
    HeldOwner owner;
    std::vector<std::shared_ptr<Probe>> tasks;
    std::vector<std::future<void>> completions;
    for (int i = 0; i < 8; ++i) {
      auto task = std::make_shared<Probe>(owner);
      completions.push_back(task->completed.get_future());
      CHECK(owner.executor.submitRpc(task, 10) == WL_OK);
      tasks.push_back(task);
    }
    auto rejected = std::make_shared<Probe>(owner);
    CHECK(owner.executor.submitRpc(rejected, 10) == WL_ERR_QUEUE_FULL);
    add_request_value_t request{};
    request.has_left = request.has_right = true;
    add_response_value_t response{};
    const auto synchronous = calculator_endpoint_add_sync(&owner.endpoint, &request, &response, 10);
    CHECK(synchronous.status == WL_RPC_FAILED && synchronous.local_error == WL_ERR_BUSY);

    CHECK(owner.executor.submitRpc(tasks[0], 10) == WL_ERR_INVALID_STATE);
    CHECK(owner.executor.cancelRpc(tasks[0].get()));
    CHECK(!owner.executor.cancelRpc(tasks[0].get()));
    owner.now = 110; // Queued time consumes the entire RPC deadline.
    if (shutdown) owner.executor.requestStop();
    owner.release();
    for (std::size_t i = 0; i < tasks.size(); ++i) {
      CHECK(completions[i].wait_for(3s) == std::future_status::ready);
      CHECK(tasks[i]->thread != std::this_thread::get_id());
      CHECK(tasks[i]->result.status == (shutdown || i == 0 ? WL_RPC_CANCELLED : WL_RPC_TIMED_OUT));
      CHECK(tasks[i]->submissions == (shutdown || i == 0 ? 0 : 1));
    }
    owner.executor.stop();
    CHECK(rejected->submissions == 0 && rejected->finishes == 0);
    CHECK(owner.executor.stats().m_rpc_completed == 8);
    CHECK(owner.executor.submitRpc(rejected, 10) == WL_ERR_NOT_INITIALIZED);
  }
}

int main() {
  values_concurrency_and_lifetime();
  cancellation_timeout_and_close();
  destruction_finishes_pending();
  response_cancel_close_races();
  bounded_queue_and_queued_deadline();
  std::puts("native async ownership, concurrency, cancellation, deadlines and shutdown PASS");
}
