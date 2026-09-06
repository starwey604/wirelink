/* SPDX-License-Identifier: Apache-2.0 */
#include "bridge.h"
#include <cstdint>
#include <cstdio>
#include <memory>

int main() {
  for (int native : {0, 1}) {
    std::unique_ptr<void, decltype(&clock_bridge_destroy)> pair(
        clock_bridge_create(60000U, native), clock_bridge_destroy);
    if (!pair || clock_bridge_reads(pair.get()) != 0U) return 1;
    if (clock_bridge_start(pair.get(), 20, 22, 1000U) != 0) return 2;
    if (clock_bridge_reads(pair.get()) != 1U) return 3;
    for (int i = 0; i < 6; ++i) {
      const auto reads = clock_bridge_reads(pair.get());
      if (clock_bridge_step(pair.get()) != 0) return 4;
      if (clock_bridge_reads(pair.get()) != reads + 2U) return 5;
    }
    const auto reads = clock_bridge_reads(pair.get());
    std::int32_t sum{};
    if (clock_bridge_result(pair.get(), &sum) != 1 || sum != 42) return 6;
    clock_bridge_close(pair.get());
    if (clock_bridge_result(pair.get(), &sum) != 1 || sum != 42) return 7;
    if (clock_bridge_step(pair.get()) != -1 || clock_bridge_reads(pair.get()) != reads) return 8;
  }
  std::puts("C++ -> exported C -> generated endpoint: OK (native/manual clocks)");
}
