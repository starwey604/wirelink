/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_WAIT_H
#define WIRELINK_WAIT_H

#include "wirelink/types.h"
#include "wirelink/clock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-owned waiting, not a clock or a protocol timer. Descriptors are
 * copied at setup; user_data remains alive until endpoint close. wait sleeps
 * until activity/notification or at most maximum_ms (UINT32_MAX = unbounded).
 * It never invokes Wirelink/user callbacks or drives the endpoint. Readiness
 * must be level-triggered or latched so notification before wait is not lost.
 * Return WL_OK or WL_ERR_NO_DATA to drive another owner pass; other errors
 * interrupt the synchronous call (WL_ERR_CANCELLED is an orderly stop).
 * Manual-clock providers must advance that same clock and notify the waiter.
 * Producers publish before notify. The owner acknowledges an old latch BEFORE
 * inspecting all state covered by that latch, or consumes it only in wait.
 * Clearing/resetting after a readiness check can erase a concurrent publish.
 * A wake may cover partial RX, external work, a manual clock update or stop;
 * it need not correspond to a protocol event. Spurious wakes must be finite.
 * All protocol operations remain on one owner; the C core never waits. */
typedef struct {
  wl_err_t (*wait)(void *user_data, uint32_t maximum_ms);
  void *user_data;
  /* Optional thread-safe, latched wake for background executors. No endpoint
   * work is performed here. Producers must stop before destroying the waiter. */
  void (*notify)(void *user_data);
} wl_waiter_t;

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
