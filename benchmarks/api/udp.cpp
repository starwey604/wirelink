/* SPDX-License-Identifier: Apache-2.0 */
#include "perf_endpoint.h"
#include "tutorial_host.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using Clock = std::chrono::steady_clock;
static void check(bool ok) { if (!ok) std::abort(); }
static double cpu_ns() {
#ifdef _WIN32
  FILETIME created, exited, kernel, user;
  check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != 0);
  auto ticks = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32U) | t.dwLowDateTime; };
  return double(ticks(kernel) + ticks(user)) * 100.0;
#else
  const auto ticks = std::clock();
  check(ticks != std::clock_t(-1));
  return double(ticks) * 1e9 / CLOCKS_PER_SEC;
#endif
}
static double elapsed_ns(Clock::time_point begin) {
  return std::chrono::duration<double, std::nano>(Clock::now() - begin).count();
}

struct Server {
  Clock::time_point started;
  double cpu_started = 0;
  unsigned calls = 0;
  unsigned expected = 0;
  bool measuring = false;
};
static int32_t echo(void *context, const echo_request_value_t *request, echo_response_value_t *response) {
  auto &server = *static_cast<Server *>(context);
  if (request->sequence == 0) {
    server.calls = 0;
    server.measuring = true;
    server.started = Clock::now();
    server.cpu_started = cpu_ns();
  } else if (request->sequence == UINT32_MAX) {
    const double cpu = cpu_ns() - server.cpu_started;
    const double wall = elapsed_ns(server.started);
    check(server.measuring && server.calls == server.expected);
    server.measuring = false;
    std::printf("{\"role\":\"server\",\"calls\":%u,\"cpu_ns_per_call\":%.3f,\"wall_ns_per_call\":%.3f}\n",
        server.calls, cpu / server.calls, wall / server.calls);
    std::fflush(stdout);
  } else if (server.measuring) {
    ++server.calls;
  }
  response->has_sequence = response->has_body = true;
  response->sequence = request->sequence;
  response->body.length = request->body.length;
  std::memcpy(response->body.data, request->body.data, request->body.length);
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 7) {
    std::fprintf(stderr, "usage: %s server|client LOCAL PEER SAMPLES WARMUP BYTES\n", argv[0]);
    return 2;
  }
  const bool is_server = std::strcmp(argv[1], "server") == 0;
  if (!is_server && std::strcmp(argv[1], "client") != 0) return 2;
  int32_t local, peer, samples, warmup, bytes = 32;
  if (!example_int32(argv[2], &local) || !example_int32(argv[3], &peer) ||
      !example_int32(argv[4], &samples) || !example_int32(argv[5], &warmup) ||
      !example_int32(argv[6], &bytes) || local < 1 || local > UINT16_MAX ||
      peer < 1 || peer > UINT16_MAX || local == peer || samples < 1 || samples > 1000000 ||
      warmup < 0 || warmup > 1000000 || bytes < 0 || bytes > 1024) return 2;
  static perf_endpoint_t endpoint;
  Server server;
  server.expected = static_cast<unsigned>(samples);
  perf_endpoint_config_t config;
  check(perf_endpoint_config_defaults(&config, wl_platform_environment()) == WL_OK);
  if (is_server) { config.user_data = &server; config.on_echo = echo; }
  check(perf_endpoint_init_config(&endpoint, &config) == WL_OK);
  auto *udp = example_udp_open(perf_endpoint_handle(&endpoint), static_cast<uint16_t>(local), static_cast<uint16_t>(peer));
  check(udp != nullptr);
  if (is_server) {
    std::puts("READY");
    std::fflush(stdout);
    while (example_running()) {
      check(perf_endpoint_step(&endpoint) == WL_OK);
      check(example_udp_wait(udp, 100) == WL_OK);
    }
  } else {
    echo_request_value_t request{};
    echo_response_value_t response;
    request.has_sequence = request.has_body = true;
    request.body.length = static_cast<uint16_t>(bytes);
    std::memset(request.body.data, 0x5a, sizeof(request.body.data));
    auto call = [&](uint32_t sequence) {
      request.sequence = sequence;
      const auto result = perf_endpoint_echo_sync(&endpoint, &request, &response, 1500);
      check(result.status == WL_RPC_SUCCESS && response.sequence == sequence);
      check(response.body.length == request.body.length &&
          std::memcmp(response.body.data, request.body.data, response.body.length) == 0);
    };
    for (int32_t i = 1; i <= warmup; ++i) call(static_cast<uint32_t>(i));
    call(0); // Exclude handshake/warmup; mark the server's measurement window.
    std::vector<double> times(static_cast<size_t>(samples));
    const double cpu_begin = cpu_ns();
    const auto wall_begin = Clock::now();
    for (int32_t i = 0; i < samples; ++i) {
      const auto begin = Clock::now();
      call(static_cast<uint32_t>(i + 1));
      times[static_cast<size_t>(i)] = elapsed_ns(begin);
    }
    const double wall = elapsed_ns(wall_begin);
    const double cpu = cpu_ns() - cpu_begin;
    call(UINT32_MAX);
    std::sort(times.begin(), times.end());
    auto quantile = [&](double fraction) {
      return times[static_cast<size_t>(std::ceil(fraction * times.size())) - 1];
    };
    std::printf("{\"role\":\"client\",\"calls\":%d,\"payload_bytes\":%d,"
        "\"cpu_ns_per_call\":%.3f,\"wall_ns_per_call\":%.3f,\"calls_per_second\":%.3f,"
        "\"p50_ns\":%.3f,\"p95_ns\":%.3f,\"p99_ns\":%.3f,\"max_ns\":%.3f}\n",
        samples, bytes, cpu / samples, wall / samples, samples * 1e9 / wall,
        quantile(0.5), quantile(0.95), quantile(0.99), times.back());
  }
  check(perf_endpoint_close(&endpoint) == WL_OK);
  example_udp_close(udp);
}
