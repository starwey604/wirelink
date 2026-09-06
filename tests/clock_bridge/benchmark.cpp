/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry_runtime.h"
#include "wirelink/host/clock.hpp"
#include "wirelink/port.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void check(bool ok) { if (!ok) std::abort(); }
static double cpu_ns() {
#ifdef _WIN32
  FILETIME created, exited, kernel, user;
  check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != 0);
  const auto ticks = [](FILETIME value) {
    return (std::uint64_t(value.dwHighDateTime) << 32U) | value.dwLowDateTime;
  };
  return double(ticks(kernel) + ticks(user)) * 100.0;
#else
  const auto ticks = std::clock();
  check(ticks != std::clock_t(-1));
  return double(ticks) * 1e9 / CLOCKS_PER_SEC;
#endif
}
struct Clock {
  std::uint32_t reads{};
  bool native{};
  static wl_time_ms_t now(void *user) {
    auto& clock = *static_cast<Clock*>(user);
    ++clock.reads;
    return clock.native ? wirelink::host::monotonic_now_ms(nullptr) : 60000U;
  }
};

template<class Action>
static void measure(const char *name, Clock& clock, unsigned expected_reads, Action action) {
  constexpr unsigned count = 2000000;
  for (unsigned i = 0; i < 1000; ++i) action();
  const auto reads = clock.reads;
  const auto cpu = cpu_ns();
  const auto started = std::chrono::steady_clock::now();
  for (unsigned i = 0; i < count; ++i) action();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  const auto cpu_elapsed = cpu_ns() - cpu;
  check(clock.reads - reads == count * expected_reads);
  std::printf("%s iterations=%u wall_ns/op=%.2f cpu_ns/op=%.2f clock_reads/op=%u\n",
      name, count, std::chrono::duration<double, std::nano>(elapsed).count() / count,
      cpu_elapsed / count, expected_reads);
}

int main() {
  static telemetry_endpoint_t endpoint;
  Clock clock;
  check(telemetry_endpoint_init(&endpoint, 1U, {Clock::now, &clock}) == WL_OK);
  wl_ctx_t *link = wl_endpoint_link(telemetry_endpoint_handle(&endpoint));
  const auto sink = [](void*, wl_io_token_t, const std::uint8_t*, std::size_t) -> wl_sink_result_t {
    return WL_SINK_SENT;
  };
  check(wl_set_sink(link, sink, nullptr) == WL_OK);
  measure("explicit_pump_idle", clock, 0, [&] {
    wl_pump_result_t result;
    check(wl_pump_step(link, 60000U, 16U, nullptr, &result) == WL_OK);
  });
  measure("endpoint_manual_idle", clock, 1, [&] {
    check(telemetry_endpoint_step(&endpoint) == WL_OK);
  });
  telemetry_endpoint_close(&endpoint);
  clock.native = true; /* Change clock domains only across a closed lifetime. */
  check(telemetry_endpoint_init(&endpoint, 2U, {Clock::now, &clock}) == WL_OK);
  link = wl_endpoint_link(telemetry_endpoint_handle(&endpoint));
  check(wl_set_sink(link, sink, nullptr) == WL_OK);
  measure("endpoint_native_idle", clock, 1, [&] {
    check(telemetry_endpoint_step(&endpoint) == WL_OK);
  });
  telemetry_t value;
  telemetry_clear(&value);
  value.has_sample = value.has_temperature_centi_c = true;
  value.sample = 1;
  value.temperature_centi_c = 2350;
  measure("unreliable_typed_submit", clock, 0, [&] {
    check(telemetry_endpoint_send_telemetry(&endpoint, &value).domain == TELEMETRY_SEND_OK);
  });
  std::printf("sizeof(wl_endpoint_t)=%zu sizeof(telemetry_endpoint_t)=%zu\n",
      sizeof(wl_endpoint_t), sizeof(telemetry_endpoint_t));
  telemetry_endpoint_close(&endpoint);
}
