/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/wait.h"
#include <errno.h>

wl_err_t wl_zephyr_waiter_init(wl_zephyr_waiter_t *waiter) {
  if (waiter == NULL) return WL_ERR_INVALID_ARG;
  atomic_clear(&waiter->private_stopped);
  return k_sem_init(&waiter->private_activity, 0, 1) == 0 ? WL_OK : WL_ERR_INVALID_ARG;
}

static wl_err_t wait_activity(void *context, uint32_t maximum_ms) {
  wl_zephyr_waiter_t *waiter = context;
  int result;
  if (waiter == NULL) return WL_ERR_INVALID_ARG;
  if (k_is_in_isr()) return WL_ERR_REENTRANT;
  if (atomic_get(&waiter->private_stopped)) return WL_ERR_CANCELLED;
  result = k_sem_take(&waiter->private_activity,
      maximum_ms == UINT32_MAX ? K_FOREVER : K_MSEC(maximum_ms));
  if (atomic_get(&waiter->private_stopped)) return WL_ERR_CANCELLED;
  return result == 0 ? WL_OK : result == -EAGAIN || result == -EBUSY ? WL_ERR_NO_DATA : WL_ERR_IO;
}

void wl_zephyr_waiter_notify(wl_zephyr_waiter_t *waiter) {
  if (waiter != NULL) k_sem_give(&waiter->private_activity);
}

static void notify_activity(void *context) { wl_zephyr_waiter_notify(context); }

wl_waiter_t wl_zephyr_waiter_descriptor(wl_zephyr_waiter_t *waiter) {
  const wl_waiter_t descriptor = {wait_activity, waiter, notify_activity};
  return descriptor;
}

void wl_zephyr_waiter_stop(wl_zephyr_waiter_t *waiter) {
  if (waiter == NULL) return;
  atomic_set(&waiter->private_stopped, 1);
  wl_zephyr_waiter_notify(waiter);
}

static wl_time_ms_t monotonic_now(void *context) {
  (void)context;
  return k_uptime_get_32();
}

wl_clock_t wl_zephyr_monotonic_clock(void) {
  const wl_clock_t clock = {monotonic_now, NULL};
  return clock;
}
