/* SPDX-License-Identifier: Apache-2.0 */

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/fifo.h"

#define STRESS_ITERATIONS UINT32_C(750000)
#define STRESS_CAPACITY UINT32_C(64)
#define HOLD_CAPACITY UINT32_C(8)
#define SLOT_STORAGE_BYTES 128U
#define MAX_IDLE_AFTER_DONE UINT32_C(10000000)
#define FULL_PROBES UINT32_C(10000)

struct stress_record {
  uint64_t sequence;
  uint64_t inverse;
  uint64_t lanes[12];
};

union stress_storage {
  max_align_t align;
  uint8_t bytes[STRESS_CAPACITY * SLOT_STORAGE_BYTES];
};

union hold_storage {
  max_align_t align;
  uint8_t bytes[HOLD_CAPACITY * SLOT_STORAGE_BYTES];
};

static wl_fifo_t stress_fifo;
static union stress_storage stress_slots;
static _Atomic uint32_t stress_producer_done;
static _Atomic int stress_producer_error;

static wl_fifo_t hold_fifo;
static union hold_storage hold_slots;
static _Atomic uint32_t hold_full_observed;
static _Atomic uint32_t hold_release_allowed;
static _Atomic int hold_producer_error;

static void fill_record(struct stress_record *record, uint64_t sequence) {
  size_t index;

  record->sequence = sequence;
  record->inverse = ~sequence;
  for (index = 0U; index < ARRAY_SIZE(record->lanes); index++) {
    record->lanes[index] =
        (UINT64_C(0x9e3779b97f4a7c15) * sequence) ^
        (UINT64_C(0xd1b54a32d192ed03) * (index + 1U));
  }
}

static bool record_is_consistent(const struct stress_record *record) {
  struct stress_record expected;

  fill_record(&expected, record->sequence);
  return memcmp(record, &expected, sizeof(expected)) == 0;
}

static int init_fifo(wl_fifo_t *fifo, void *storage, size_t storage_size,
                     uint32_t capacity) {
  const wl_fifo_config_t config = {
      .value_size = sizeof(struct stress_record),
      .value_alignment = _Alignof(struct stress_record),
      .capacity = capacity,
  };
  const wl_fifo_storage_t fifo_storage = {
      .data = storage,
      .size = storage_size,
  };
  wl_fifo_requirements_t requirements;
  int result;

  result = wl_fifo_requirements(&config, &requirements);
  if (result != WL_OK || requirements.slot_count != capacity ||
      requirements.storage_size > storage_size ||
      requirements.slot_stride < sizeof(struct stress_record)) {
    return result == WL_OK ? WL_ERR_BUF_TOO_SMALL : result;
  }
  return wl_fifo_init(fifo, &config, &fifo_storage);
}

static void *stress_producer(void *argument) {
  uint64_t sequence;

  (void)argument;
  for (sequence = 1U; sequence <= STRESS_ITERATIONS; sequence++) {
    wl_fifo_write_claim_t claim;
    int result;

    do {
      result = wl_fifo_write_claim(&stress_fifo, &claim);
      if (result == WL_ERR_QUEUE_FULL) {
        sched_yield();
      }
    } while (result == WL_ERR_QUEUE_FULL);
    if (result != WL_OK || claim.value == NULL ||
        claim.value_size != sizeof(struct stress_record)) {
      atomic_store_explicit(&stress_producer_error,
                            result == WL_OK ? WL_ERR_INVALID_STATE : result,
                            memory_order_relaxed);
      break;
    }

    /* Encode-like producer path: populate the final FIFO slot in place. */
    fill_record(claim.value, sequence);
    result = wl_fifo_write_publish(&stress_fifo, &claim);
    if (result != WL_OK) {
      atomic_store_explicit(&stress_producer_error, result,
                            memory_order_relaxed);
      break;
    }
    if ((sequence & UINT64_C(0x3ff)) == 0U) {
      sched_yield();
    }
  }
  atomic_store_explicit(&stress_producer_done, 1U, memory_order_release);
  return NULL;
}

static void *blocked_head_producer(void *argument) {
  uint32_t probe;
  wl_fifo_write_claim_t claim;
  int result;

  (void)argument;
  for (probe = 0U; probe < FULL_PROBES; probe++) {
    result = wl_fifo_write_claim(&hold_fifo, &claim);
    if (result != WL_ERR_QUEUE_FULL) {
      if (result == WL_OK) {
        (void)wl_fifo_write_abort(&hold_fifo, &claim);
        result = WL_ERR_INVALID_STATE;
      }
      atomic_store_explicit(&hold_producer_error, result,
                            memory_order_relaxed);
      return NULL;
    }
  }
  atomic_store_explicit(&hold_full_observed, 1U, memory_order_release);

  while (atomic_load_explicit(&hold_release_allowed, memory_order_acquire) ==
         0U) {
    sched_yield();
  }

  do {
    result = wl_fifo_write_claim(&hold_fifo, &claim);
    if (result == WL_ERR_QUEUE_FULL) {
      sched_yield();
    }
  } while (result == WL_ERR_QUEUE_FULL);
  if (result != WL_OK) {
    atomic_store_explicit(&hold_producer_error, result, memory_order_relaxed);
    return NULL;
  }
  fill_record(claim.value, HOLD_CAPACITY + 1U);
  result = wl_fifo_write_publish(&hold_fifo, &claim);
  if (result != WL_OK) {
    atomic_store_explicit(&hold_producer_error, result, memory_order_relaxed);
  }
  return NULL;
}

ZTEST(wirelink_fifo_pthread, test_parallel_spsc_preserves_every_record) {
  uint64_t expected_sequence = 1U;
  uint32_t idle_after_done = 0U;
  pthread_t producer;
  wl_fifo_stats_t stats;

  memset(&stress_fifo, 0, sizeof(stress_fifo));
  memset(&stress_slots, 0, sizeof(stress_slots));
  atomic_init(&stress_producer_done, 0U);
  atomic_init(&stress_producer_error, WL_OK);
  zassert_ok(init_fifo(&stress_fifo, stress_slots.bytes,
                       sizeof(stress_slots.bytes), STRESS_CAPACITY));
  zassert_equal(pthread_create(&producer, NULL, stress_producer, NULL), 0);

  while (expected_sequence <= STRESS_ITERATIONS &&
         idle_after_done < MAX_IDLE_AFTER_DONE) {
    wl_fifo_view_t view;
    const struct stress_record *record;
    int result = wl_fifo_read_acquire(&stress_fifo, &view);

    if (result == WL_ERR_NO_DATA) {
      if (atomic_load_explicit(&stress_producer_done, memory_order_acquire) !=
          0U) {
        idle_after_done++;
      }
      sched_yield();
      continue;
    }
    zassert_ok(result);
    zassert_equal(view.value_size, sizeof(struct stress_record));
    record = view.value;
    zassert_true(record_is_consistent(record), "torn record at %llu",
                 (unsigned long long)expected_sequence);
    zassert_equal(record->sequence, expected_sequence,
                  "FIFO reordered, duplicated, or dropped a record");

    /* Let the producer fill every other slot while this head stays borrowed. */
    if ((expected_sequence & UINT64_C(0xfff)) == 0U) {
      uint32_t yields;
      for (yields = 0U; yields < 32U; yields++) {
        sched_yield();
      }
      zassert_true(record_is_consistent(record));
      zassert_equal(record->sequence, expected_sequence);
    }
    zassert_ok(wl_fifo_read_release(&stress_fifo, &view));
    expected_sequence++;
  }

  zassert_equal(pthread_join(producer, NULL), 0);
  zassert_equal(atomic_load_explicit(&stress_producer_error,
                                     memory_order_relaxed),
                WL_OK);
  zassert_equal(expected_sequence, (uint64_t)STRESS_ITERATIONS + 1U);
  zassert_ok(wl_fifo_get_stats(&stress_fifo, &stats));
  zassert_equal(stats.depth, 0U);
  zassert_equal(stats.publishes, STRESS_ITERATIONS);
  zassert_equal(stats.consumes, STRESS_ITERATIONS);
  zassert_true(stats.high_watermark > 0U);
  zassert_true(stats.high_watermark <= STRESS_CAPACITY);
  zassert_equal(stats.errors, 0U);
}

ZTEST(wirelink_fifo_pthread, test_full_never_overwrites_borrowed_head) {
  struct stress_record snapshot;
  wl_fifo_view_t held_view;
  pthread_t producer;
  uint64_t sequence;
  uint32_t waits = 0U;
  wl_fifo_stats_t stats;

  memset(&hold_fifo, 0, sizeof(hold_fifo));
  memset(&hold_slots, 0, sizeof(hold_slots));
  atomic_init(&hold_full_observed, 0U);
  atomic_init(&hold_release_allowed, 0U);
  atomic_init(&hold_producer_error, WL_OK);
  zassert_ok(init_fifo(&hold_fifo, hold_slots.bytes, sizeof(hold_slots.bytes),
                       HOLD_CAPACITY));

  for (sequence = 1U; sequence <= HOLD_CAPACITY; sequence++) {
    wl_fifo_write_claim_t claim;
    zassert_ok(wl_fifo_write_claim(&hold_fifo, &claim));
    fill_record(claim.value, sequence);
    zassert_ok(wl_fifo_write_publish(&hold_fifo, &claim));
  }
  zassert_ok(wl_fifo_read_acquire(&hold_fifo, &held_view));
  memcpy(&snapshot, held_view.value, sizeof(snapshot));
  zassert_equal(snapshot.sequence, 1U);
  zassert_equal(pthread_create(&producer, NULL, blocked_head_producer, NULL),
                0);

  while (atomic_load_explicit(&hold_full_observed, memory_order_acquire) ==
             0U &&
         waits < MAX_IDLE_AFTER_DONE) {
    waits++;
    sched_yield();
  }
  zassert_true(waits < MAX_IDLE_AFTER_DONE);
  zassert_mem_equal(held_view.value, &snapshot, sizeof(snapshot),
                    "full producer modified the borrowed oldest slot");
  zassert_ok(wl_fifo_read_release(&hold_fifo, &held_view));
  atomic_store_explicit(&hold_release_allowed, 1U, memory_order_release);
  zassert_equal(pthread_join(producer, NULL), 0);
  zassert_equal(atomic_load_explicit(&hold_producer_error,
                                     memory_order_relaxed),
                WL_OK);

  for (sequence = 2U; sequence <= HOLD_CAPACITY + 1U; sequence++) {
    wl_fifo_view_t view;
    const struct stress_record *record;
    zassert_ok(wl_fifo_read_acquire(&hold_fifo, &view));
    record = view.value;
    zassert_true(record_is_consistent(record));
    zassert_equal(record->sequence, sequence);
    zassert_ok(wl_fifo_read_release(&hold_fifo, &view));
  }
  zassert_equal(wl_fifo_read_acquire(&hold_fifo, &held_view), WL_ERR_NO_DATA);
  zassert_ok(wl_fifo_get_stats(&hold_fifo, &stats));
  zassert_equal(stats.depth, 0U);
  zassert_equal(stats.high_watermark, HOLD_CAPACITY);
  zassert_equal(stats.publishes, HOLD_CAPACITY + 1U);
  zassert_equal(stats.consumes, HOLD_CAPACITY + 1U);
  zassert_true(stats.full_rejections >= FULL_PROBES);
  zassert_equal(stats.errors, 0U);
}

ZTEST_SUITE(wirelink_fifo_pthread, NULL, NULL, NULL, NULL, NULL);
