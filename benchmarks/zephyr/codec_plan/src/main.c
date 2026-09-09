/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include <zephyr/kernel.h>
#include <zephyr/timing/timing.h>
#include <cmsis_core.h>
static codec_plan_fixture_t fixtures[CODEC_PLAN_KINDS];
int main(void) {
  timing_init(); timing_start();
  printk("codec_plan_begin_v1,hz=%llu,lto=%u,barriers=1\n", (unsigned long long)timing_freq_get(), IS_ENABLED(CONFIG_LTO));
  /* mode 0: warm homogeneous; mode 1: I-cache invalidated before each sample;
   * mode 2: round-robin messages. No invalidation is inside measured cycles. */
  for (unsigned mode = 0; mode < 3; ++mode)
    for (unsigned kind = 0; kind < (mode == 2 ? 1U : CODEC_PLAN_KINDS); ++kind)
      for (unsigned pattern = 0; pattern < CODEC_PLAN_PATTERNS; ++pattern)
        for (unsigned stage = 0; stage < CODEC_PLAN_STAGES; ++stage) {
          for (unsigned k = 0; k < CODEC_PLAN_KINDS; ++k)
            __ASSERT_NO_MSG(codec_plan_init(&fixtures[k], k, pattern, stage) == WL_CODEC_OK);
          for (unsigned i = 0; i < 32; ++i)
            __ASSERT_NO_MSG(codec_plan_step(&fixtures[mode == 2 ? i % CODEC_PLAN_KINDS : kind]) == WL_CODEC_OK);
          for (unsigned r = 0; r < 5; ++r) {
            uint64_t total = 0;
            const unsigned batch = mode == 2 ? 36U : 32U;
            for (unsigned i = 0; i < batch; ++i) {
              codec_plan_fixture_t *f = &fixtures[mode == 2 ? i % CODEC_PLAN_KINDS : kind];
              unsigned lock = irq_lock();
              if (mode == 1) SCB_InvalidateICache();
              timing_t start = timing_counter_get();
              compiler_barrier();
              int result = codec_plan_step(f);
              compiler_barrier();
              timing_t end = timing_counter_get();
              irq_unlock(lock);
              __ASSERT_NO_MSG(result == WL_CODEC_OK);
              __ASSERT_NO_MSG(stage != 2 || f->output_length == f->expected_length);
              total += timing_cycles_get(&start, &end);
            }
            for (unsigned k = 0; k < CODEC_PLAN_KINDS; ++k)
              __ASSERT_NO_MSG(codec_plan_check(&fixtures[k]) == WL_CODEC_OK);
            printk("codec_plan_v1,m=%u,k=%u,p=%u,s=%u,r=%u,n=%u,cycles=%llu\n",
                mode, kind, pattern, stage, r, batch, (unsigned long long)total);
          }
          k_sleep(K_MSEC(10));
        }
  printk("codec_plan_end_v1,groups=225,samples=1125,result=pass\n");
  return 0;
}
