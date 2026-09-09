/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include "mixed_endpoint.h"
#include <zephyr/kernel.h>
#include <zephyr/timing/timing.h>

static timing_t start;
static unsigned irq_key, ticks;
static uint64_t total_cycles, max_cycles;

void mixed_h7_case_begin(void) {
  ticks = 0U;
  total_cycles = max_cycles = 0U;
}

void mixed_h7_tick_begin(void) {
  irq_key = irq_lock();
  start = timing_counter_get();
  compiler_barrier();
}

void mixed_h7_tick_end(void) {
  compiler_barrier();
  timing_t end = timing_counter_get();
  irq_unlock(irq_key);
  const uint64_t cycles = timing_cycles_get(&start, &end);
  total_cycles += cycles;
  if (cycles > max_cycles) max_cycles = cycles;
  ++ticks;
}

void mixed_h7_case_end(const char *scenario, int envelope) {
  __ASSERT_NO_MSG(ticks == 2400U);
  printk("mixed_cpu_v1,scenario=%s,e=%d,n=%u,cycles=%llu,max=%llu\n",
      scenario, envelope, ticks, (unsigned long long)total_cycles,
      (unsigned long long)max_cycles);
  /* Neither reporting nor RTT drain belongs to the timed owner work. */
  k_sleep(K_MSEC(20));
}

void mixed_retry_cpu(void);

int main(void) {
  timing_init();
  timing_start();
  printk("mixed_h7_begin_v1,hz=%llu,lto=%u,coexist=%u,endpoint_bytes=%u,barriers=1\n",
      (unsigned long long)timing_freq_get(), IS_ENABLED(CONFIG_LTO),
      MIXED_EXPECT_COEXIST, (unsigned)sizeof(mixed_endpoint_t));
  __ASSERT_NO_MSG(mixed_traffic_run(MIXED_EXPECT_COEXIST != 0) == 0);
  mixed_retry_cpu();
  printk("mixed_h7_end_v1,result=pass,traffic_rows=42,cpu_groups=21,retry_samples=360\n");
  return 0;
}
