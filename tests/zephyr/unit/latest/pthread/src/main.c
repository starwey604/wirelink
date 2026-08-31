/* SPDX-License-Identifier: Apache-2.0 */

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/latest.h"

#define STRESS_ITERATIONS UINT32_C(500000)
#define MAX_IDLE_AFTER_DONE UINT32_C(10000000)

struct stress_value {
  uint32_t sequence;
  uint32_t inverse;
  uint64_t lanes[12];
};

static wl_latest_t mailbox;
static struct stress_value slots[WL_LATEST_SLOT_COUNT];
static _Atomic uint32_t producer_done;
static _Atomic uint32_t producer_error;

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

static void *producer(void *argument) {
  uint32_t sequence;

  (void)argument;
  for (sequence = 1U; sequence <= STRESS_ITERATIONS; sequence++) {
    wl_latest_write_claim_t claim;

    if (wl_latest_write_claim(&mailbox, &claim) != WL_OK) {
      atomic_store_explicit(&producer_error, 1U, memory_order_relaxed);
      break;
    }
    fill_value(claim.value, sequence);
    if (wl_latest_write_publish(&mailbox, &claim) != WL_OK) {
      atomic_store_explicit(&producer_error, 1U, memory_order_relaxed);
      break;
    }
    if ((sequence & UINT32_C(0x3ff)) == 0U) {
      sched_yield();
    }
  }
  atomic_store_explicit(&producer_done, 1U, memory_order_release);
  return NULL;
}

ZTEST(wirelink_latest_pthread, test_parallel_spsc_never_tears_payload) {
  const wl_latest_config_t config = {
      .value_size = sizeof(slots[0]),
      .value_alignment = _Alignof(struct stress_value),
  };
  const wl_latest_storage_t storage = {slots, sizeof(slots)};
  uint32_t idle_after_done = 0U;
  uint32_t last_sequence = 0U;
  pthread_t thread;
  wl_latest_stats_t stats;

  memset(&mailbox, 0, sizeof(mailbox));
  memset(slots, 0, sizeof(slots));
  atomic_init(&producer_done, 0U);
  atomic_init(&producer_error, 0U);
  zassert_ok(wl_latest_init(&mailbox, &config, &storage));
  zassert_equal(pthread_create(&thread, NULL, producer, NULL), 0);

  while (last_sequence != STRESS_ITERATIONS &&
         idle_after_done < MAX_IDLE_AFTER_DONE) {
    wl_latest_view_t view;
    int result = wl_latest_read_acquire(&mailbox, &view);

    if (result == WL_ERR_NO_DATA) {
      if (atomic_load_explicit(&producer_done, memory_order_acquire) != 0U) {
        idle_after_done++;
      }
      sched_yield();
      continue;
    }
    zassert_ok(result);
    zassert_true(value_is_consistent(view.value));
    zassert_true(((const struct stress_value *)view.value)->sequence >
                 last_sequence);
    last_sequence = ((const struct stress_value *)view.value)->sequence;
    zassert_ok(wl_latest_read_release(&mailbox, &view));
  }

  zassert_equal(pthread_join(thread, NULL), 0);
  zassert_equal(atomic_load_explicit(&producer_error, memory_order_relaxed),
                0U);
  zassert_equal(last_sequence, STRESS_ITERATIONS);
  zassert_ok(wl_latest_get_stats(&mailbox, &stats));
  zassert_equal(stats.publishes, STRESS_ITERATIONS);
  zassert_equal(stats.reads + stats.coalesced, STRESS_ITERATIONS);
  zassert_equal(stats.errors, 0U);
}

ZTEST_SUITE(wirelink_latest_pthread, NULL, NULL, NULL, NULL, NULL);
