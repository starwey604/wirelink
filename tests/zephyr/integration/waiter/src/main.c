/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include "wirelink/zephyr/wait.h"

static wl_zephyr_waiter_t waiter;
static wl_waiter_t descriptor;
static struct k_thread worker;
K_THREAD_STACK_DEFINE(worker_stack, 2048);
K_SEM_DEFINE(entering, 0, 1);
static unsigned iterations;
static wl_err_t outcome;

static void run(void *a, void *b, void *c) {
  (void)a; (void)b; (void)c;
  for (unsigned i = 0; i < iterations; ++i) {
    k_sem_give(&entering);
    outcome = descriptor.wait(descriptor.user_data, UINT32_MAX);
    if (outcome != WL_OK) return;
  }
}
static void start(unsigned count) {
  zassert_ok(wl_zephyr_waiter_init(&waiter));
  descriptor = wl_zephyr_waiter_descriptor(&waiter);
  iterations = count;
  outcome = WL_ERR_INVALID_STATE;
  k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
      run, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
}
ZTEST(wirelink_waiter, test_notify_before_wait_and_timeout) {
  zassert_ok(wl_zephyr_waiter_init(&waiter));
  descriptor = wl_zephyr_waiter_descriptor(&waiter);
  descriptor.notify(descriptor.user_data);
  zassert_equal(descriptor.wait(descriptor.user_data, 0), WL_OK);
  zassert_equal(descriptor.wait(descriptor.user_data, 0), WL_ERR_NO_DATA);
  zassert_equal(descriptor.wait(descriptor.user_data, 2), WL_ERR_NO_DATA);
  wl_zephyr_waiter_stop(&waiter);
  zassert_equal(descriptor.wait(descriptor.user_data, UINT32_MAX), WL_ERR_CANCELLED);
}
ZTEST(wirelink_waiter, test_notify_races_wait_entry) {
  start(200);
  for (unsigned i = 0; i < 200; ++i) {
    zassert_ok(k_sem_take(&entering, K_SECONDS(2)));
    descriptor.notify(descriptor.user_data);
  }
  zassert_ok(k_thread_join(&worker, K_SECONDS(2)));
  zassert_equal(outcome, WL_OK);
}
ZTEST(wirelink_waiter, test_stop_wakes_unbounded_wait) {
  start(1);
  zassert_ok(k_sem_take(&entering, K_SECONDS(2)));
  k_msleep(2);
  wl_zephyr_waiter_stop(&waiter);
  zassert_ok(k_thread_join(&worker, K_SECONDS(2)));
  zassert_equal(outcome, WL_ERR_CANCELLED);
  const wl_clock_t clock = wl_zephyr_monotonic_clock();
  zassert_not_null(clock.now_ms);
  const uint32_t before = clock.now_ms(clock.user_data);
  k_msleep(2);
  zassert_true(clock.now_ms(clock.user_data) - before >= 2);
}
ZTEST_SUITE(wirelink_waiter, NULL, NULL, NULL, NULL, NULL);
