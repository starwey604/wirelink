/* SPDX-License-Identifier: Apache-2.0 */
// Deterministic available-work batches, not timed batching in the executor.
#include "wirelink/host/executor.hpp"
#include "wirelink/frame.h"
#include "wirelink/cobs.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
using wirelink::host::Executor;
using wirelink::host::ExecutorActivity;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::abort(); } } while (0)

template<class F> static void eventually(F ready) {
  const auto end = std::chrono::steady_clock::now() + 5s;
  while (!ready()) {
    CHECK(std::chrono::steady_clock::now() < end);
    std::this_thread::sleep_for(50us); // Harness only; no wall-time assertions.
  }
}
struct Workload {
  Executor executor;
  wl_endpoint_t endpoint{};
  std::array<uint8_t, 512> payload{}, control{};
  std::array<uint8_t, 1024> unit{}, fallback{};
  std::array<uint8_t, 8192> fifo{};
  std::mutex mutex;
  std::condition_variable gate;
  bool entered{}, released{};
  bool asynchronous{}, blocked{}, refill{}, application_followup{}, rpc_required{}, rpc_seen{};
  bool reply_on_rx{}, legacy_service{};
  std::atomic<bool> unblock{};
  unsigned sent{}, received{}, services{}, applications{}, max_per_pass{}, in_pass{};
  wl_io_token_t token{};
  const uint8_t *pending{};
  size_t length{};
  std::array<bool, 8> seen{};

  static int service(void *p) noexcept {
    auto& w = *static_cast<Workload *>(p);
    ++w.services; w.in_pass = 0;
    {
      std::unique_lock lock(w.mutex);
      if (!w.released) {
        w.entered = true; w.gate.notify_one();
        w.gate.wait(lock, [&] { return w.released; });
      }
    }
    if (w.pending && (!w.blocked || w.unblock.load())) {
      w.consume(w.pending, w.length);
      w.pending = nullptr;
      CHECK(wl_tx_complete(&w.executor.context(), w.token, WL_OK) == WL_OK);
      return WL_OK;
    }
    return WL_ERR_NO_DATA;
  }
  static uint32_t hint(const void *p, wl_time_ms_t) noexcept {
    const auto& w = *static_cast<const Workload *>(p);
    return w.pending && (!w.blocked || w.unblock.load()) ? 0 : WL_POLL_NO_DEADLINE_MS;
  }
  void consume(const uint8_t *data, size_t size) {
    wl_frame_view_t frame;
    std::array<uint8_t, 1024> raw;
    size_t decoded = 0;
    CHECK(size > 0 && data[size - 1] == 0);
    CHECK(wl_cobs_decode(data, size - 1, raw.data(), raw.size(), &decoded) == WL_OK);
    CHECK(wl_frame_decode(raw.data(), decoded, WL_INTEGRITY_NONE, &frame) == WL_OK);
    CHECK(frame.message_id >= 100 && frame.message_id < 108);
    CHECK(frame.payload.length == 1 && frame.payload.data[0] == frame.message_id - 100);
    if (!refill) CHECK(!seen[frame.message_id - 100]);
    seen[frame.message_id - 100] = true;
    ++sent;
    if (refill && sent == 1) rx(1);
    // A continuously replenished lane must yield to RX/application progress.
    if (refill && sent > Executor::s_kLatestDispatchBudget) {
      CHECK(received == 1);
      if (rpc_required) CHECK(rpc_seen);
    }
  }
  static wl_sink_result_t sink(void *p, wl_io_token_t token, const uint8_t *data, size_t size) {
    auto& w = *static_cast<Workload *>(p);
    ++w.in_pass;
    if (w.max_per_pass < w.in_pass) w.max_per_pass = w.in_pass;
    if (w.asynchronous) {
      CHECK(w.pending == nullptr);
      w.pending = data; w.length = size; w.token = token;
      return WL_SINK_STARTED;
    }
    w.consume(data, size);
    if (w.refill) {
      if (w.sent == 9) w.executor.requestStop();
      else {
        const uint8_t value = 0;
        CHECK(w.executor.submitLatest(100, &value, 1) == WL_OK);
      }
    }
    return WL_SINK_SENT;
  }
  static wl_pump_event_disposition_t event(void *p, wl_ctx_t& ctx,
      const wl_event_t& event, wl_time_ms_t) noexcept {
    auto& w = *static_cast<Workload *>(p);
    if (event.type == WL_EVT_UNRELIABLE_RX) {
      CHECK(event.message_id == 200 && event.payload_len == 1 && event.payload[0] == 42);
      ++w.received;
      if (w.reply_on_rx) {
        const uint8_t value = 0;
        CHECK(wl_send_unreliable(&ctx, 100, &value, 1) == WL_OK);
      }
      wl_event_release(&ctx, &event);
      return WL_PUMP_EVENT_CONSUMED;
    }
    return WL_PUMP_EVENT_UNHANDLED;
  }
  static bool application(void *p, wl_ctx_t&, wl_time_ms_t) noexcept {
    auto& w = *static_cast<Workload *>(p);
    ++w.applications;
    return w.application_followup && w.applications < 3;
  }
  void start() {
    wl_config_t config{};
    config.max_payload_len = 512; config.envelope = WL_ENVELOPE_COBS_STREAM;
    config.integrity = WL_INTEGRITY_NONE; config.session_id = 1;
    wl_storage_t storage{payload.data(), payload.size(), unit.data(), unit.size(),
      control.data(), control.size(), fifo.data(), fifo.size(), fallback.data(), fallback.size()};
    if (rpc_required) {
      const auto clock = wirelink::host::monotonic_clock();
      wl_pump_hooks_t hooks{};
      hooks.adapter_user_data = hooks.application_user_data = this;
      hooks.application_progress = [](void *p, wl_ctx_t *ctx, wl_time_ms_t now) -> uint8_t {
        return application(p, *ctx, now);
      };
      hooks.on_event = [](void *p, wl_ctx_t *ctx, const wl_event_t *e, wl_time_ms_t now) {
        return event(p, *ctx, *e, now);
      };
      CHECK(wl_endpoint_init(&endpoint, &config, &storage, &clock, &hooks) == WL_OK);
      hooks.service = service;
      hooks.adapter_deadline_hint = hint;
      CHECK(wl_endpoint_attach(&endpoint, &hooks) == WL_OK);
      const wl_endpoint_driver_t driver{&endpoint, &endpoint,
        [](void *p) { return wl_endpoint_step(static_cast<wl_endpoint_t *>(p), 64); },
        [](void *p) -> wl_err_t { wl_endpoint_close(static_cast<wl_endpoint_t *>(p)); return WL_OK; }, 0U};
      CHECK(executor.initialize(driver) == WL_OK);
    } else {
      CHECK(executor.initialize(config, storage) == WL_OK);
      wirelink::host::ExecutorHooks hooks{};
      hooks.m_user_data = this; hooks.m_service = service; hooks.m_on_event = event;
      hooks.m_application_progress = application;
      if (!legacy_service) hooks.m_adapter_deadline_hint = hint;
      CHECK(executor.setHooks(hooks) == WL_OK);
    }
    CHECK(executor.setSink(sink, this) == WL_OK);
    CHECK(executor.start() == WL_OK);
    std::unique_lock lock(mutex);
    CHECK(gate.wait_for(lock, 5s, [&] { return entered; }));
  }
  void release() {
    std::lock_guard lock(mutex); released = true; gate.notify_one();
  }
  void rx(unsigned count) {
    std::array<uint8_t, 512> frames{};
    size_t length = 0;
    const uint8_t payload = 42;
    wl_wire_packet_t packet{};
    packet.type = WL_PACKET_DATA; packet.message_id = 200;
    packet.payload = &payload; packet.payload_len = 1;
    for (unsigned i = 0; i < count; ++i) {
      size_t frame_size = 0;
      CHECK(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM, frames.data() + length,
          frames.size() - length, &frame_size) == WL_OK);
      length += frame_size;
    }
    size_t accepted = 0;
    CHECK(executor.feedBytes(frames.data(), length, accepted) == WL_OK);
    CHECK(accepted == length);
  }
};
static void scenario(const char *name, unsigned tx, unsigned rx, bool async = false,
    bool blocked = false, bool refill = false) {
  Workload w;
  w.asynchronous = async; w.blocked = blocked; w.refill = refill;
  w.application_followup = std::strcmp(name, "application_followup") == 0;
  w.rpc_required = std::strcmp(name, "rpc_refill_stop") == 0;
  w.reply_on_rx = std::strstr(name, "rx_reply") != nullptr;
  w.legacy_service = std::strstr(name, "legacy") != nullptr;
  w.start();
  std::thread caller;
  if (w.rpc_required) {
    const auto* proxy = wl_endpoint_rpc_executor(&w.endpoint);
    CHECK(proxy != nullptr);
    caller = std::thread([&w, proxy] {
      const wl_rpc_sync_call_t call{&w,
        [](void *p, wl_time_ms_t, wl_rpc_sync_notify_fn notify, void *context, wl_rpc_call_t *) -> wl_err_t {
          auto& w = *static_cast<Workload *>(p);
          CHECK(w.sent == Executor::s_kLatestDispatchBudget);
          w.rpc_seen = true;
          wl_rpc_completion_t result{};
          result.status = WL_RPC_SUCCESS;
          notify(context, &result);
          return WL_OK;
        }};
      CHECK(proxy->invoke(proxy->context, &call, 5000).status == WL_RPC_SUCCESS);
    });
    eventually([&] { return w.executor.stats().m_rpc_submitted == 1; });
  }
  for (unsigned i = 0; i < tx; ++i) {
    const uint8_t value = static_cast<uint8_t>(i);
    CHECK(w.executor.submitLatest(uint16_t(100 + i), &value, 1) == WL_OK);
  }
  if (rx) w.rx(rx);
  if (std::strcmp(name, "empty_feed") == 0) {
    for (unsigned i = 0; i < 64; ++i) {
      size_t accepted = 123;
      CHECK(w.executor.feedBytes(nullptr, 0, accepted) == WL_OK && accepted == 0);
    }
  }
  w.release();
  if (refill) eventually([&] { return w.executor.state() == Executor::State::kStopped; });
  else {
    eventually([&] { return w.executor.activity().get(ExecutorActivity::waits) >= 1; });
    if (blocked) {
      const auto passes = w.executor.activity().get(ExecutorActivity::passes);
      std::this_thread::sleep_for(3ms);
      CHECK(w.executor.activity().get(ExecutorActivity::passes) == passes);
      w.unblock = true; w.executor.notify();
      eventually([&] { return w.executor.activity().get(ExecutorActivity::waits) >= 2; });
    }
  }
  w.executor.stop();
  if (caller.joinable()) caller.join();
  CHECK(w.sent == (refill ? 9 : tx + unsigned(w.reply_on_rx)) && w.received == (refill ? 1 : rx));
  CHECK(w.max_per_pass <= Executor::s_kLatestDispatchBudget);
  const auto stats = w.executor.stats();
  CHECK(stats.m_latest_failed == 0 && stats.m_poll_errors == 0 && stats.m_service_errors == 0);
  const auto activity = w.executor.activity();
  CHECK(activity.enabled);
  CHECK(activity.get(ExecutorActivity::rpc_empty_collections) == 0);
  CHECK(w.applications == activity.get(ExecutorActivity::passes));
  if (w.application_followup) CHECK(w.applications >= 3);
  if (w.rpc_required) {
    CHECK(w.rpc_seen);
    CHECK(activity.get(ExecutorActivity::rpc_jobs) == 1);
    CHECK(activity.get(ExecutorActivity::rpc_batches) == 1);
    CHECK(activity.get(ExecutorActivity::rpc_completions) == 1);
  }
  if (!async && tx && !refill) {
    CHECK(activity.get(ExecutorActivity::latest_deferred) == 0);
    CHECK(activity.get(ExecutorActivity::passes) <=
        (tx + Executor::s_kLatestDispatchBudget - 1) / Executor::s_kLatestDispatchBudget + 1);
    CHECK(activity.get(ExecutorActivity::no_reported_work) == 0);
  }
  std::printf("{\"case\":\"%s\",\"budget\":%zu,\"delivered\":%u,\"received\":%u,\"max_per_pass\":%u",
      name, Executor::s_kLatestDispatchBudget, w.sent, w.received, w.max_per_pass);
  for (unsigned i = 0; i < activity.counts.size(); ++i)
    std::printf(",\"%s\":%llu", wirelink::host::executor_activity_names[i],
        static_cast<unsigned long long>(activity.counts[i]));
  std::puts("}");
}
static void overflow_notifies() {
  Workload w;
  w.start(); // Owner cannot drain while checking producer publication.
  size_t accepted = 0;
  std::array<uint8_t, 8192> bytes{};
  CHECK(w.executor.feedBytes(bytes.data(), bytes.size(), accepted) == WL_OK);
  CHECK(accepted == bytes.size());
  const auto before = w.executor.activity().get(ExecutorActivity::notifications);
  CHECK(w.executor.feedBytes(bytes.data(), 1, accepted) == WL_ERR_WOULD_BLOCK);
  CHECK(accepted == 0);
  CHECK(w.executor.activity().get(ExecutorActivity::notifications) == before + 1);
  w.release();
  eventually([&] { return w.executor.activity().get(ExecutorActivity::waits) >= 1; });
  w.rx(1); // Overflow recovery must leave the endpoint usable.
  eventually([&] { return w.executor.stats().m_rx_events >= 1; });
  w.executor.stop();
  CHECK(w.received == 1);
}
int main() {
  scenario("idle", 0, 0);
  scenario("rx_burst", 0, 8);
  scenario("latest_one", 1, 0);
  scenario("latest_burst", 8, 0);
  scenario("mixed_burst", 8, 8);
  scenario("async_latest", 8, 0, true);
  scenario("legacy_async_latest", 8, 0, true);
  scenario("blocked_latest", 8, 0, true, true);
  scenario("async_rx_reply", 0, 1, true);
  scenario("legacy_async_rx_reply", 0, 1, true);
  scenario("empty_feed", 0, 0);
  scenario("refill_stop", 1, 0, false, false, true);
  scenario("application_followup", 0, 0);
  scenario("rpc_refill_stop", 1, 0, false, false, true);
  overflow_notifies();
}
