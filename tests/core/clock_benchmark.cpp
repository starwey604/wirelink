/* SPDX-License-Identifier: Apache-2.0 */
#include "bridge.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
static void check(bool ok) { if (!ok) std::abort(); }
int main() {
  for (int native : {0, 1}) {
    std::unique_ptr<void, decltype(&clock_bridge_destroy)> owner(
      clock_bridge_create(60000, native), clock_bridge_destroy);
    check(owner != nullptr);
    constexpr unsigned count = 2000000;
    const auto reads = clock_bridge_reads(owner.get());
    const auto cpu = std::clock();
    const auto began = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < count; ++i) check(clock_bridge_step(owner.get()) == 0);
    const auto elapsed = std::chrono::steady_clock::now() - began;
    const auto cpu_elapsed = std::clock() - cpu;
    check(cpu != std::clock_t(-1) && cpu_elapsed >= 0);
    check(clock_bridge_reads(owner.get()) - reads == count);
    std::printf("clock_bridge_v2 native=%d iterations=%u wall_ns/pass=%.2f cpu_ns/pass=%.2f outer_reads/pass=1 core_reads/pass=0\n",
      native, count, std::chrono::duration<double, std::nano>(elapsed).count() / count,
      double(cpu_elapsed) * 1e9 / CLOCKS_PER_SEC / count);
  }
}
