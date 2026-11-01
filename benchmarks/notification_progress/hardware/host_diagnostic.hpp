/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WL_RESEARCH_HOST_DIAGNOSTIC_HPP
#define WL_RESEARCH_HOST_DIAGNOSTIC_HPP
#include <array>
#include <atomic>
#include <iostream>
namespace diag_host {
enum Event { notify, merged, consumed, wait, wait_success, service, ready, count };
inline std::array<std::atomic<unsigned long long>, count> counters{};
inline std::array<unsigned long long, count> first{};
inline void add(Event e) { counters[e].fetch_add(1, std::memory_order_relaxed); }
inline void start() { for (unsigned i = 0; i < count; ++i) first[i] = counters[i].load(std::memory_order_relaxed); }
inline void stop() {
  const char *names[] = {"notify", "merged", "consumed", "wait", "wait_success", "service", "ready"};
  std::cout << "{\"event\":\"host_diagnostic\",\"counts\":{";
  for (unsigned i = 0; i < count; ++i)
    std::cout << (i ? "," : "") << '"' << names[i] << "\":" << counters[i].load(std::memory_order_relaxed) - first[i];
  std::cout << "}}\n";
}
}
#endif
