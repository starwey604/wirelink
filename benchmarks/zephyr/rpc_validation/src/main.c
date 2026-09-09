/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include <zephyr/kernel.h>
#include <zephyr/timing/timing.h>
static rpc_validation_fixture_t fixture;
int main(void) {
  timing_init(); timing_start();
  printk("rpc_validation_begin_v2,hz=%llu,batch=32\n", (unsigned long long)timing_freq_get());
  const size_t lengths[] = {0, 16, 32, 256, 512};
  for (unsigned kind = 0; kind < 3; ++kind)
    for (unsigned stage = 0; stage < RPC_VALIDATION_STAGES; ++stage)
      for (size_t b = 0; b < ARRAY_SIZE(lengths); ++b) {
        if (kind == 0 && lengths[b] > 32) continue;
        __ASSERT_NO_MSG(rpc_validation_init(&fixture, kind, stage, lengths[b]) == WL_CODEC_OK);
        for (unsigned i = 0; i < 16; ++i) __ASSERT_NO_MSG(rpc_validation_step(&fixture) == WL_CODEC_OK);
        __ASSERT_NO_MSG(rpc_validation_check(&fixture) == WL_CODEC_OK);
        for (unsigned r = 0; r < 5; ++r) {
          int error = WL_CODEC_OK;
          unsigned key = irq_lock();
          timing_t start = timing_counter_get();
          for (unsigned i = 0; i < 32; ++i) {
            error = rpc_validation_step(&fixture);
            if (error != WL_CODEC_OK) break;
          }
          timing_t end = timing_counter_get();
          irq_unlock(key);
          __ASSERT_NO_MSG(error == WL_CODEC_OK && rpc_validation_check(&fixture) == WL_CODEC_OK);
          printk("rpc_validation_v2,k=%u,s=%u,b=%u,r=%u,n=32,cycles=%llu\n", kind, stage,
              (unsigned)lengths[b], r, (unsigned long long)timing_cycles_get(&start, &end));
        }
        k_sleep(K_MSEC(20));
      }
  printk("rpc_validation_end_v2,groups=65,samples=325,result=pass\n");
  return 0;
}
