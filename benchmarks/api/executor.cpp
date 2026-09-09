/* SPDX-License-Identifier: Apache-2.0 */
#include "perf_endpoint.h"
#include "wirelink/loopback.h"
#include "wirelink/frame.h"
#include "wirelink/host/executor.hpp"
#include <algorithm>
#include <array>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using Clock = std::chrono::steady_clock;
using wirelink::host::Executor;
namespace diag = wirelink::diagnostics;
static void check(bool ok) { if (!ok) std::abort(); }
static double ns(Clock::duration d) { return std::chrono::duration<double, std::nano>(d).count(); }
static double cpu() {
#ifdef _WIN32
  FILETIME created, exited, kernel, user;
  check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != 0);
  auto ticks = [](FILETIME value) {
    return (uint64_t(value.dwHighDateTime) << 32U) | value.dwLowDateTime;
  };
  return double(ticks(kernel) + ticks(user)) * 100.0;
#else
  const auto value = std::clock();
  check(value != std::clock_t(-1));
  return double(value) * 1e9 / CLOCKS_PER_SEC;
#endif
}

struct Pair {
  perf_endpoint_t client{}, server{};
  wl_loopback_t cable{};
  static int32_t echo(void *, const echo_request_value_t *in, echo_response_value_t *out) {
    out->has_sequence = out->has_body = true;
    out->sequence = in->sequence;
    out->body.length = in->body.length;
    std::memcpy(out->body.data, in->body.data, in->body.length);
    return 0;
  }
  Pair() {
    // Identity source is setup-only; protocol clock is thread-safe steady time.
    uint64_t identity = 1;
    wl_environment_t env{wirelink::host::monotonic_clock(),
      {[](void *p, uint64_t *out) -> wl_err_t { *out = ++*static_cast<uint64_t *>(p); return WL_OK; }, &identity}};
    check(perf_endpoint_init(&client, env) == WL_OK);
    perf_endpoint_config_t config;
    check(perf_endpoint_config_defaults(&config, env) == WL_OK);
    config.on_echo = echo;
    check(perf_endpoint_init_config(&server, &config) == WL_OK);
    check(wl_loopback_connect(&cable, perf_endpoint_handle(&client), perf_endpoint_handle(&server)) == WL_OK);
  }
  static wl_err_t step(void *context) {
    auto &p = *static_cast<Pair *>(context);
    // Both peers belong to this one owner. No concurrent access to loopback.
    for (unsigned i = 0; i < 2; ++i) {
      check(perf_endpoint_step(&p.server) == WL_OK);
      check(perf_endpoint_step(&p.client) == WL_OK);
    }
    return WL_OK;
  }
  static wl_err_t close(void *context) {
    auto &p = *static_cast<Pair *>(context);
    check(perf_endpoint_close(&p.client) == WL_OK);
    return perf_endpoint_close(&p.server);
  }
  wl_endpoint_driver_t driver() { return {perf_endpoint_handle(&client), this, step, close}; }
};

struct Latest {
  std::array<uint8_t, 512> tx{}, control{};
  std::array<uint8_t, 1024> unit{}, fifo{}, fallback{};
  std::array<std::atomic<uint32_t>, 8> last{};
  unsigned bytes = 32;
  bool shared = false;
  void init(Executor &executor) {
    wl_config_t config{};
    config.max_payload_len = 512;
    config.envelope = WL_ENVELOPE_NATIVE_PACKET;
    config.integrity = WL_INTEGRITY_NONE;
    config.session_id = 1;
    config.ack_timeout_ms = 100;
    config.max_transmission_unit = 1024;
    const wl_storage_t storage{tx.data(), tx.size(), unit.data(), unit.size(),
      control.data(), control.size(), fifo.data(), fifo.size(), fallback.data(), fallback.size()};
    check(executor.initialize(config, storage) == WL_OK);
    check(executor.setSink(sink, this) == WL_OK);
  }
  static wl_sink_result_t sink(void *context, wl_io_token_t, const uint8_t *data, size_t size) {
    auto &s = *static_cast<Latest *>(context);
    wl_frame_view_t frame{};
    check(wl_frame_decode(data, size, WL_INTEGRITY_NONE, &frame) == WL_OK);
    check(frame.payload.length == s.bytes && frame.message_id >= 500 && frame.message_id < 508);
    uint32_t sequence, lane;
    std::memcpy(&sequence, frame.payload.data, 4);
    std::memcpy(&lane, frame.payload.data + 4, 4);
    check(lane < 8 && frame.message_id == 500 + (s.shared ? 0 : lane));
    for (unsigned i = 8; i < s.bytes; ++i)
      check(frame.payload.data[i] == uint8_t(sequence + lane));
    // Shared-lane replacement can hide whole updates, but cannot tear/reorder
    // the surviving updates from any one producer.
    check(sequence > s.last[lane].load(std::memory_order_relaxed));
    s.last[lane].store(sequence, std::memory_order_release);
    return WL_SINK_SENT;
  }
};

static unsigned number(const char *text, unsigned maximum) {
  char *end;
  const unsigned long value = std::strtoul(text, &end, 10);
  check(*text && *end == '\0' && value <= maximum);
  return static_cast<unsigned>(value);
}
int main(int argc, char **argv) {
#if defined(WIRELINK_HOST_PROFILING)
  const char *profile = "full";
#elif defined(WIRELINK_HOST_LOCK_PROFILING)
  const char *profile = "locks";
#else
  const char *profile = "off";
#endif
  if (argc != 6) {
    std::fprintf(stderr, "usage: %s rpc|proxy|latest|shared PRODUCERS CALLS_EACH BYTES PERIOD_US\n", argv[0]);
    return 2;
  }
  const std::string mode = argv[1];
  const bool rpc = mode == "rpc", proxy = mode == "proxy", shared = mode == "shared";
  check(rpc || proxy || shared || mode == "latest");
  const unsigned producers = number(argv[2], 8), count = number(argv[3], 1000000);
  const unsigned bytes = number(argv[4], 512), period = number(argv[5], 1000000);
  check(producers && count && bytes >= 8);
  auto pair = (rpc || proxy) ? std::make_unique<Pair>() : nullptr;
  Latest latest;
  latest.bytes = bytes; latest.shared = shared;
  Executor executor;
  if (pair) check(executor.initialize(pair->driver()) == WL_OK);
  else latest.init(executor);
  std::vector<std::vector<double>> latency(producers, std::vector<double>(count));
  std::vector<std::thread> threads;
  std::barrier ready(static_cast<std::ptrdiff_t>(producers + 1));
  std::atomic<uint64_t> rejected{};
  diag::reset(); // No instrumented workers exist yet.
  check(executor.start() == WL_OK);
  for (unsigned lane = 0; lane < producers; ++lane) threads.emplace_back([&, lane] {
    std::array<uint8_t, 512> payload{};
    echo_request_value_t request{};
    echo_response_value_t response;
    request.has_sequence = request.has_body = true;
    request.body.length = static_cast<uint16_t>(bytes);
    std::memset(request.body.data, int(lane), bytes);
    ready.arrive_and_wait();
    auto deadline = Clock::now();
    for (unsigned i = 0; i < count; ++i) {
      if (period) {
        deadline += std::chrono::microseconds(period);
        std::this_thread::sleep_until(deadline);
      }
      request.sequence = i + 1;
      uint32_t sequence = i + 1;
      std::memcpy(payload.data(), &sequence, 4);
      std::memcpy(payload.data() + 4, &lane, 4);
      std::memset(payload.data() + 8, int(uint8_t(sequence + lane)), bytes - 8);
      const auto start = Clock::now();
      if (rpc) {
        const auto result = perf_endpoint_echo_sync(&pair->client, &request, &response, 5000);
        if (result.status != WL_RPC_SUCCESS) {
          std::fprintf(stderr, "RPC failure: status=%d local=%d\n", result.status, result.local_error);
          std::abort();
        }
        check(response.sequence == sequence && response.body.length == bytes &&
          std::memcmp(response.body.data, request.body.data, bytes) == 0);
      } else if (proxy) {
        // Isolate executor handoff/CV lifetime. No wire/codec cost in this mode.
        wl_rpc_sync_call_t call{&sequence,
          [](void *p, wl_time_ms_t, wl_rpc_sync_notify_fn done, void *context, wl_rpc_call_t *) -> wl_err_t {
            check(*static_cast<uint32_t *>(p) != 0);
            wl_rpc_completion_t result{}; result.status = WL_RPC_SUCCESS;
            done(context, &result); return WL_OK;
          }};
        const auto *bridge = wl_endpoint_rpc_executor(perf_endpoint_handle(&pair->client));
        check(bridge->invoke(bridge->context, &call, 5000).status == WL_RPC_SUCCESS);
      } else {
        const auto result = executor.submitLatest(uint16_t(500 + (shared ? 0 : lane)), payload.data(), bytes);
        if (result != WL_OK) ++rejected;
      }
      latency[lane][i] = ns(Clock::now() - start);
    }
  });
  const auto begin = Clock::now();
  const double cpu_begin = cpu();
  ready.arrive_and_wait();
  for (auto &thread : threads) thread.join();
  if (!pair) {
    // Prove idle transition delivers the final value, not just fast submission.
    if (shared) {
      std::array<uint8_t, 512> final{};
      uint32_t sequence = count + 1, lane = 0;
      std::memcpy(final.data(), &sequence, 4); std::memcpy(final.data() + 4, &lane, 4);
      std::memset(final.data() + 8, int(uint8_t(sequence)), bytes - 8);
      check(executor.submitLatest(500, final.data(), bytes) == WL_OK);
    }
    const auto limit = Clock::now() + std::chrono::seconds(5);
    for (;;) {
      bool done = true;
      for (unsigned i = 0; i < (shared ? 1 : producers); ++i)
        done &= latest.last[i].load(std::memory_order_acquire) == count + unsigned(shared);
      if (done) break;
      check(Clock::now() < limit);
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  }
  const auto wall = ns(Clock::now() - begin);
  const double used_cpu = cpu() - cpu_begin;
  executor.stop();
  check(rejected == 0);
  const auto stats = executor.stats();
  check(stats.m_latest_failed == 0 && stats.m_poll_errors == 0 && stats.m_service_errors == 0);
  if (pair) check(stats.m_rpc_submitted == uint64_t(producers) * count && stats.m_rpc_completed == stats.m_rpc_submitted);
  else check(stats.m_latest_submitted == uint64_t(producers) * count + unsigned(shared));
  std::vector<double> all;
  for (const auto &lane : latency) all.insert(all.end(), lane.begin(), lane.end());
  std::sort(all.begin(), all.end());
  auto quantile = [&](double q) { return all[std::min(all.size() - 1, size_t(q * all.size()))]; };
  const auto total = uint64_t(producers) * count;
  std::printf("{\"profile\":\"%s\",\"abi\":%u,\"mode\":\"%s\",\"producers\":%u,\"calls\":%llu,\"bytes\":%u,\"period_us\":%u,"
    "\"wall_ns\":%.0f,\"cpu_ns_per_call\":%.3f,\"calls_per_second\":%.3f,"
    "\"p50_ns\":%.3f,\"p95_ns\":%.3f,\"p99_ns\":%.3f,\"max_ns\":%.3f,"
    "\"dispatched\":%llu,\"coalesced\":%llu,\"errors\":0}\n", profile, PERF_RUNTIME_CODEGEN_ABI_VERSION, mode.c_str(), producers,
    static_cast<unsigned long long>(total), bytes, period, wall, used_cpu / total,
    total * 1e9 / wall, quantile(.5), quantile(.95), quantile(.99), all.back(),
    static_cast<unsigned long long>(stats.m_latest_dispatched), static_cast<unsigned long long>(stats.m_latest_coalesced));
  diag::dump(stdout, 0); // All workers stopped; never print from hot paths.
}
