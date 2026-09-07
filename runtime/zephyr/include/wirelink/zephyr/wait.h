/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_WAIT_H
#define WIRELINK_ZEPHYR_WAIT_H

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "wirelink/wait.h"
#include "wirelink/clock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One waiting owner task, any number of notifying producer tasks/ISRs.
 * Static, stable storage; initialize before attach and join the owner and all
 * producers before reinitializing/destroying it. No internal thread or heap. */
typedef struct {
  struct k_sem private_activity;
  atomic_t private_stopped;
} wl_zephyr_waiter_t;

wl_err_t wl_zephyr_waiter_init(wl_zephyr_waiter_t *waiter);
wl_waiter_t wl_zephyr_waiter_descriptor(wl_zephyr_waiter_t *waiter);
/* Latches activity. Invoke AFTER committing RX/completion or advancing a
 * synchronized manual clock; never lose an event just before the owner sleeps. */
void wl_zephyr_waiter_notify(wl_zephyr_waiter_t *waiter);
/* Stop is latched and wakes a blocked sync call with WL_ERR_CANCELLED. */
void wl_zephyr_waiter_stop(wl_zephyr_waiter_t *waiter);
wl_clock_t wl_zephyr_monotonic_clock(void);

#ifdef __cplusplus
}
#endif
#endif
