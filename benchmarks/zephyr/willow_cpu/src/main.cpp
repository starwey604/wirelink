/* SPDX-License-Identifier: Apache-2.0 */
#define WL_SPIN_PROFILE_IMPLEMENTATION
#include "spin_profile.hpp"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wirelink_cpu, LOG_LEVEL_INF);
int willow_hil_main();
static k_tid_t owner;
K_SEM_DEFINE(ready, 0, 1);

static void report(void *, void *, void *) {
  k_sem_take(&ready, K_FOREVER);
  k_thread_runtime_stats_t previous_owner{}, previous_cpu{};
  (void)k_thread_runtime_stats_get(owner, &previous_owner);
  (void)k_thread_runtime_stats_all_get(&previous_cpu);
  auto previous_lock = spin_snapshot();
  auto previous_ms = k_uptime_get();
  unsigned window = 0;
  for (;;) {
    k_sleep(K_SECONDS(2));
    const auto now_ms = k_uptime_get();
    k_thread_runtime_stats_t current_owner{}, current_cpu{};
    const int a = k_thread_runtime_stats_get(owner, &current_owner);
    const int b = k_thread_runtime_stats_all_get(&current_cpu);
    __ASSERT(a == 0 && b == 0, "CPU statistics unavailable");
    const auto locks = spin_snapshot();
    // Scheduler accounting, not wall time of owner passes. IRQ attribution
    // follows Zephyr's architecture; this is not an IRQ-exclusive instruction count.
    LOG_INF("wl_h7_cpu_v1,w=%u,ms=%lld,hz=%llu,owner=%llu,busy=%llu,total=%llu",
      window, (long long)(now_ms - previous_ms), (unsigned long long)timing_freq_get(),
      (unsigned long long)(current_owner.execution_cycles - previous_owner.execution_cycles),
      (unsigned long long)(current_cpu.total_cycles - previous_cpu.total_cycles),
      (unsigned long long)(current_cpu.execution_cycles - previous_cpu.execution_cycles));
    LOG_INF("wl_h7_lock_v1,w=%u,n=%llu,wait=%llu,hold=%llu,waitmax=%u,holdmax=%u,on=%u",
      window++, (unsigned long long)(locks.calls - previous_lock.calls),
      (unsigned long long)(locks.wait_cycles - previous_lock.wait_cycles),
      (unsigned long long)(locks.hold_cycles - previous_lock.hold_cycles),
      locks.wait_max, locks.hold_max, IS_ENABLED(WL_H7_LOCK_PROFILING));
    previous_owner = current_owner; previous_cpu = current_cpu;
    previous_lock = locks; previous_ms = now_ms;
  }
}
K_THREAD_DEFINE(cpu_report, 4096, report, nullptr, nullptr, nullptr, 12, 0, 0);

int main() {
  owner = k_current_get();
  k_thread_name_set(owner, "wl_owner");
  LOG_INF("wl_h7_cpu_ready_v1,lock_profile=%u,no_motor_can=1", IS_ENABLED(WL_H7_LOCK_PROFILING));
  k_sem_give(&ready);
  return willow_hil_main();
}
