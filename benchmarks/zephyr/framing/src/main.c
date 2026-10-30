/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include <zephyr/kernel.h>
#include <zephyr/timing/timing.h>

static framing_fixture_t fixture;
int main(void) {
  /* Let the USB Serial/JTAG console attach after reset before printing. */
  k_sleep(K_MSEC(2000));
  timing_init();
  timing_start();
#if defined(WL_FRAMING_COBS_CRC16_ONLY)
  const char *matrix = "cobs-crc16";
#else
  const char *matrix = "multi";
#endif
  printk("framing_cpu_begin_v2,hz=%llu,context_bytes=%u,irq_masked_batch=32,matrix=%s\n",
      (unsigned long long)timing_freq_get(), (unsigned)sizeof(wl_ctx_t), matrix);
  unsigned groups = 0U;
  for (unsigned mode = 0U; mode < FRAMING_MODES; ++mode)
    for (int envelope = WL_ENVELOPE_COBS_STREAM; envelope <= WL_ENVELOPE_BUS_LENGTH16; ++envelope)
      for (int integrity = WL_INTEGRITY_NONE; integrity <= WL_INTEGRITY_CRC32C; ++integrity)
        for (unsigned pattern = 0U; pattern < 3U; ++pattern) {
#if defined(WL_FRAMING_COBS_CRC16_ONLY)
          if (envelope != WL_ENVELOPE_COBS_STREAM || integrity != WL_INTEGRITY_CRC16) continue;
#endif
          if ((mode == FRAMING_RAW_DECODE || mode == FRAMING_COBS_DECODE) &&
              envelope != WL_ENVELOPE_COBS_STREAM) continue;
          const size_t sizes[] = {32U, 120U, 512U, 2048U};
          for (unsigned size = 0U; size < ARRAY_SIZE(sizes); ++size) {
            __ASSERT_NO_MSG(framing_init(&fixture, mode, envelope, integrity, pattern, sizes[size]) == WL_OK);
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
              printk("framing_cpu_v2,mode=%s,e=%d,i=%d,p=%u,b=%u,r=%u,n=32,cycles=%llu\n",
                  framing_mode_name(mode), envelope, integrity, pattern, (unsigned)sizes[size], repeat,
                  (unsigned long long)timing_cycles_get(&begin, &end));
            }
            /* Reporting and RTT drain are outside the timed/IRQ-masked batch. */
            k_sleep(K_MSEC(20));
            ++groups;
          }
        }
  printk("framing_cpu_end_v2,result=pass,groups=%u,samples=%u\n", groups, groups * 5U);
  return 0;
}
