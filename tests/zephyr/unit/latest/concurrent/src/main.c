/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include "wirelink/latest.h"

#define STRESS_ITERATIONS UINT32_C(50000)
#define PRODUCER_STACK_SIZE 2048

struct stress_value {
  uint32_t sequence;
  uint32_t inverse;
  uint64_t lanes[12];
};

static wl_latest_t mailbox;
static struct stress_value slots[WL_LATEST_SLOT_COUNT];
static atomic_t producer_done;
static atomic_t producer_error;
static struct k_thread producer_thread;
K_THREAD_STACK_DEFINE(producer_stack, PRODUCER_STACK_SIZE);

static void fill_value(struct stress_value *value, uint32_t sequence) {
  size_t index;

  value->sequence = sequence;
  value->inverse = ~sequence;
  for (index = 0U; index < ARRAY_SIZE(value->lanes); index++) {
    value->lanes[index] = (UINT64_C(0x9e3779b97f4a7c15) * sequence) ^
                          (UINT64_C(0xd1b54a32d192ed03) * (index + 1U));
  }
}

static bool value_is_consistent(const struct stress_value *value) {
  struct stress_value expected;

  fill_value(&expected, value->sequence);
  return memcmp(value, &expected, sizeof(expected)) == 0;
}

static void producer(void *first, void *second, void *third) {
  uint32_t sequence;

  ARG_UNUSED(first);
  ARG_UNUSED(second);
  ARG_UNUSED(third);
  for (sequence = 1U; sequence <= STRESS_ITERATIONS; sequence++) {
    wl_latest_write_claim_t claim;

    if (wl_latest_write_claim(&mailbox, &claim) != WL_OK) {
      atomic_set(&producer_error, 1);
      break;
    }
    fill_value(claim.value, sequence);
    if (wl_latest_write_publish(&mailbox, &claim) != WL_OK) {
      atomic_set(&producer_error, 1);
      break;
    }
    if ((sequence & UINT32_C(0x3ff)) == 0U) {
      k_yield();
    }
  }
  atomic_set(&producer_done, 1);
}

ZTEST(wirelink_latest_concurrent, test_spsc_never_observes_torn_value) {
  const wl_latest_config_t config = {
      .value_size = sizeof(slots[0]),
      .value_alignment = _Alignof(struct stress_value),
  };
  const wl_latest_storage_t storage = {
      .data = slots,
      .size = sizeof(slots),
  };
  uint32_t last_sequence = 0U;
  int64_t deadline;
  wl_latest_stats_t stats;

  memset(&mailbox, 0, sizeof(mailbox));
  memset(slots, 0, sizeof(slots));
  atomic_clear(&producer_done);
  atomic_clear(&producer_error);
  zassert_ok(wl_latest_init(&mailbox, &config, &storage));

  (void)k_thread_create(&producer_thread, producer_stack,
                        K_THREAD_STACK_SIZEOF(producer_stack), producer, NULL,
                        NULL, NULL, CONFIG_ZTEST_THREAD_PRIORITY, 0, K_NO_WAIT);
  deadline = k_uptime_get() + 15000;

  while (last_sequence != STRESS_ITERATIONS && k_uptime_get() < deadline) {
    wl_latest_view_t view;
    int result = wl_latest_read_acquire(&mailbox, &view);

    if (result == WL_ERR_NO_DATA) {
      k_yield();
      continue;
    }
    zassert_ok(result);
    zassert_true(value_is_consistent(view.value));
    zassert_true(((const struct stress_value *)view.value)->sequence >
                 last_sequence);
    last_sequence = ((const struct stress_value *)view.value)->sequence;
    zassert_ok(wl_latest_read_release(&mailbox, &view));
  }

  zassert_true(atomic_get(&producer_done) != 0);
  zassert_equal(atomic_get(&producer_error), 0);
  zassert_equal(last_sequence, STRESS_ITERATIONS);
  zassert_ok(wl_latest_get_stats(&mailbox, &stats));
  zassert_equal(stats.publishes, STRESS_ITERATIONS);
  zassert_equal(stats.reads + stats.coalesced, STRESS_ITERATIONS);
  zassert_equal(stats.errors, 0U);
}

ZTEST_SUITE(wirelink_latest_concurrent, NULL, NULL, NULL, NULL, NULL);
