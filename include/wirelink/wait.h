/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_WAIT_H
#define WIRELINK_WAIT_H

#include "wirelink/types.h"
#include "wirelink/clock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Rebase a relative wait limit at waiter entry, after readiness queries or
 * other owner work. Times use the endpoint's clock domain; unsigned subtraction
 * handles wrap. The elapsed interval must be less than one full clock wrap.
 * UINT32_MAX is unbounded; every other limit stays finite. This is arithmetic
 * only: it neither reads a clock nor expires work nor invokes a waiter. */
static inline uint32_t wl_wait_remaining(uint32_t maximum_ms,
    wl_time_ms_t sampled_at_ms, wl_time_ms_t now_ms) {
  if (maximum_ms == UINT32_MAX) return UINT32_MAX;
  uint32_t elapsed = now_ms - sampled_at_ms;
  return elapsed >= maximum_ms ? 0U : maximum_ms - elapsed;
}

#ifdef __cplusplus
}
#endif
#endif
