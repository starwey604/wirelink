/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_KERNEL_ZEPHYR_TIMEOUT_H_
#define WIRELINK_KERNEL_ZEPHYR_TIMEOUT_H_
#include <stdint.h>
#include <zephyr/kernel.h>
static inline k_timeout_t wl_kernel_timeout_ms(uint32_t ms) {
  if (ms == UINT32_MAX)
    return K_FOREVER;
  if (ms == 0U)
    return K_NO_WAIT;
  uint64_t ticks = k_ms_to_ticks_ceil64(ms);
#if !defined(CONFIG_TIMEOUT_64BIT)
  if (ticks > INT32_MAX)
    ticks = INT32_MAX;
#endif
  return K_TICKS((k_ticks_t)ticks);
}
#endif
