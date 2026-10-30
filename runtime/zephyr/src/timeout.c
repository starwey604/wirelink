/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/timeout.h"
#include "wirelink/zephyr/detail/timeout.h"

k_timeout_t wl_zephyr_timeout_since(k_timeout_t maximum, int64_t sampled_ticks) {
  if (K_TIMEOUT_EQ(maximum, K_FOREVER) || K_TIMEOUT_EQ(maximum, K_NO_WAIT))
    return maximum;
#if defined(CONFIG_TIMEOUT_64BIT)
  /* Zephyr reserves negative tick encodings for absolute deadlines. Keep
   * conversion here, outside callers' owner loops and the portable C core. */
  uint64_t due = Z_IS_TIMEOUT_RELATIVE(maximum)
      ? (uint64_t)sampled_ticks + (uint64_t)maximum.ticks
      : (uint64_t)Z_TICK_ABS(maximum.ticks);
  /* The largest relative value plus uptime need not fit the absolute encoding.
   * Wake earlier at its finite ceiling instead of overflowing into a sentinel. */
  if (due > INT64_MAX - 1U) due = INT64_MAX - 1U;
  return due <= (uint64_t)k_uptime_ticks() ? K_NO_WAIT : K_TIMEOUT_ABS_TICKS(due);
#else
  uint32_t elapsed = (uint32_t)k_uptime_ticks() - (uint32_t)sampled_ticks;
  return K_TICKS(elapsed >= maximum.ticks ? 0U : maximum.ticks - elapsed);
#endif
}

k_timeout_t wl_zephyr_timeout_us_since(uint32_t maximum_us, int64_t sampled_ticks) {
  return wl_zephyr_timeout_since(wl_zephyr_timeout_us(maximum_us), sampled_ticks);
}
