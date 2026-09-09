/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include <zephyr/kernel.h>
#include <zephyr/timing/timing.h>

static framing_fixture_t fixture;
int main(void) {
  timing_init();
  timing_start();
  printk("framing_h7_begin_v1,hz=%llu,context_bytes=%u,irq_masked_batch=32\n",
      (unsigned long long)timing_freq_get(), (unsigned)sizeof(wl_ctx_t));
  for (unsigned mode = 0U; mode < FRAMING_MODES; ++mode)
    for (int envelope = WL_ENVELOPE_COBS_STREAM; envelope <= WL_ENVELOPE_NATIVE_PACKET; ++envelope)
      for (unsigned pattern = 0U; pattern < 3U; ++pattern) {
        const size_t sizes[] = {32U, 120U, 512U, 2048U};
        for (unsigned size = 0U; size < ARRAY_SIZE(sizes); ++size) {
          /* Product-like NONE; host matrix additionally covers CRC16/CRC32C and length16. */
          __ASSERT_NO_MSG(framing_init(&fixture, mode, envelope, WL_INTEGRITY_NONE, pattern, sizes[size]) == WL_OK);
          for (unsigned i = 0U; i < 16U; ++i) __ASSERT_NO_MSG(framing_step(&fixture) == WL_OK);
          __ASSERT_NO_MSG(framing_check(&fixture) == WL_OK);
          for (unsigned repeat = 0U; repeat < 5U; ++repeat) {
            int error = WL_OK;
            unsigned key = irq_lock();
            timing_t begin = timing_counter_get();
            for (unsigned i = 0U; i < 32U; ++i) {
              error = framing_step(&fixture);
              if (error != WL_OK) break;
            }
            timing_t end = timing_counter_get();
            irq_unlock(key);
            __ASSERT_NO_MSG(error == WL_OK && framing_check(&fixture) == WL_OK);
            printk("framing_h7_v1,mode=%s,e=%d,i=0,p=%u,b=%u,r=%u,n=32,cycles=%llu\n",
                framing_mode_name(mode), envelope, pattern, (unsigned)sizes[size], repeat,
                (unsigned long long)timing_cycles_get(&begin, &end));
          }
          /* Reporting and RTT drain are outside the timed/IRQ-masked batch. */
          k_sleep(K_MSEC(20));
        }
      }
  printk("framing_h7_end_v1,result=pass,groups=168,samples=840\n");
  return 0;
}
