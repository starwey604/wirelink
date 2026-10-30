/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_DETAIL_TIMEOUT_H
#define WIRELINK_ZEPHYR_DETAIL_TIMEOUT_H

#include <stdint.h>
#include <zephyr/kernel.h>

/* Convert in 64 bits before narrowing. On 32-bit timeout kernels a large
 * millisecond limit can overflow K_MSEC, including into K_FOREVER. An earlier
 * finite wake is legal; the owner re-samples and recomputes its deadline. */
static inline k_timeout_t wl_zephyr_timeout_ms(uint32_t maximum_ms) {
  if (maximum_ms == UINT32_MAX) return K_FOREVER;
  if (maximum_ms == 0U) return K_NO_WAIT;
  uint64_t ticks = k_ms_to_ticks_ceil64(maximum_ms);
#if !defined(CONFIG_TIMEOUT_64BIT)
  if (ticks > INT32_MAX) ticks = INT32_MAX;
#endif
  return K_TICKS((k_ticks_t)ticks);
}

static inline k_timeout_t wl_zephyr_timeout_us(uint32_t maximum_us) {
  if (maximum_us == UINT32_MAX) return K_FOREVER;
  if (maximum_us == 0U) return K_NO_WAIT;
  uint64_t ticks = k_us_to_ticks_ceil64(maximum_us);
#if !defined(CONFIG_TIMEOUT_64BIT)
  if (ticks > INT32_MAX) ticks = INT32_MAX;
#endif
  return K_TICKS((k_ticks_t)ticks);
}

#endif
