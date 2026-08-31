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

#define COMMAND_COUNT UINT32_C(250000)
#define COMMAND_CAPACITY UINT32_C(64)
#define RESULT_CAPACITY UINT32_C(8)
#define COMMAND_STORAGE_BYTES 64U
#define RESULT_STORAGE_BYTES 32U
#define MAX_INITIAL_WAIT UINT32_C(10000000)

struct command_record {
  uint64_t sequence;
  uint64_t inverse;
  uint64_t arguments[4];
};

struct result_record {
  uint64_t sequence;
  uint64_t command_fingerprint;
  uint32_t busy_retries;
  int32_t status;
};

union command_storage {
  max_align_t align;
  uint8_t bytes[COMMAND_CAPACITY * COMMAND_STORAGE_BYTES];
};

union result_storage {
  max_align_t align;
  uint8_t bytes[RESULT_CAPACITY * RESULT_STORAGE_BYTES];
};

static wl_fifo_t command_fifo;
static wl_fifo_t result_fifo;
static union command_storage command_slots;
static union result_storage result_slots;
static _Atomic uint32_t client_done;
static _Atomic uint32_t result_full_seen;
static _Atomic int session_error;

static uint64_t mix64(uint64_t value) {
  value ^= value >> 30U;
  value *= UINT64_C(0xbf58476d1ce4e5b9);
  value ^= value >> 27U;
  value *= UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

static void fill_command(struct command_record *command, uint64_t sequence) {
  size_t index;

  command->sequence = sequence;
  command->inverse = ~sequence;
  for (index = 0U; index < ARRAY_SIZE(command->arguments); index++) {
    command->arguments[index] =
        mix64(sequence ^ (UINT64_C(0x9e3779b97f4a7c15) * (index + 1U)));
  }
}

static bool command_is_consistent(const struct command_record *command) {
  struct command_record expected;

  fill_command(&expected, command->sequence);
  return memcmp(command, &expected, sizeof(expected)) == 0;
}

static uint64_t command_fingerprint(const struct command_record *command) {
  uint64_t fingerprint = command->sequence;
  size_t index;

  for (index = 0U; index < ARRAY_SIZE(command->arguments); index++) {
    fingerprint ^= mix64(command->arguments[index] + index);
  }
  return fingerprint;
}

static int init_queue(wl_fifo_t *fifo, void *storage, size_t storage_size,
                      size_t value_size, size_t value_alignment,
                      uint32_t capacity) {
  const wl_fifo_config_t config = {
      .value_size = value_size,
      .value_alignment = value_alignment,
      .capacity = capacity,
  };
  const wl_fifo_storage_t fifo_storage = {
      .data = storage,
      .size = storage_size,
  };
  wl_fifo_requirements_t requirements;
  int result = wl_fifo_requirements(&config, &requirements);

  if (result != WL_OK || requirements.storage_size > storage_size ||
      requirements.slot_count != capacity ||
      requirements.slot_stride < value_size) {
    return result == WL_OK ? WL_ERR_BUF_TOO_SMALL : result;
  }
  return wl_fifo_init(fifo, &config, &fifo_storage);
}

static int enqueue_command(uint64_t sequence) {
  wl_fifo_write_claim_t claim;
  int result = wl_fifo_write_claim(&command_fifo, &claim);

  if (result != WL_OK) {
    return result;
  }
  if (claim.value_size != sizeof(struct command_record)) {
    (void)wl_fifo_write_abort(&command_fifo, &claim);
    return WL_ERR_INVALID_STATE;
  }
  /* A generated encoder can populate a claimed command slot the same way. */
  fill_command(claim.value, sequence);
  return wl_fifo_write_publish(&command_fifo, &claim);
}

static int publish_result(const struct result_record *result) {
  wl_fifo_write_claim_t claim;
  int status = wl_fifo_write_claim(&result_fifo, &claim);

  if (status != WL_OK) {
    return status;
  }
  if (claim.value_size != sizeof(*result)) {
    (void)wl_fifo_write_abort(&result_fifo, &claim);
    return WL_ERR_INVALID_STATE;
  }
  memcpy(claim.value, result, sizeof(*result));
  return wl_fifo_write_publish(&result_fifo, &claim);
}

static void set_session_error(int error) {
  int expected = WL_OK;

  (void)atomic_compare_exchange_strong_explicit(
      &session_error, &expected, error, memory_order_relaxed,
      memory_order_relaxed);
}

static int submit_command(const struct command_record *command,
                          uint32_t *busy_remaining) {
  if (!command_is_consistent(command)) {
    return WL_ERR_CORRUPT_PAYLOAD;
  }
  if (*busy_remaining != 0U) {
    (*busy_remaining)--;
    return WL_ERR_BUSY;
  }
  return WL_OK;
}

static void *session_consumer(void *argument) {
  wl_fifo_view_t command_view = {0};
  struct command_record command_snapshot = {0};
  struct result_record pending_result = {0};
  const void *borrowed_address = NULL;
  uint64_t next_sequence = 1U;
  uint32_t busy_remaining = 0U;
  bool command_borrowed = false;
  bool result_pending = false;

  (void)argument;
  while (next_sequence <= COMMAND_COUNT || result_pending) {
    int result;

    if (result_pending) {
      result = publish_result(&pending_result);
      if (result == WL_ERR_QUEUE_FULL) {
        atomic_store_explicit(&result_full_seen, 1U, memory_order_release);
        sched_yield();
        continue;
      }
      if (result != WL_OK) {
        set_session_error(result);
        return NULL;
      }
      result_pending = false;
      if (next_sequence > COMMAND_COUNT) {
        break;
      }
    }

    if (!command_borrowed) {
      result = wl_fifo_read_acquire(&command_fifo, &command_view);
      if (result == WL_ERR_NO_DATA) {
        if (atomic_load_explicit(&client_done, memory_order_acquire) != 0U &&
            next_sequence <= COMMAND_COUNT) {
          set_session_error(WL_ERR_NO_DATA);
          return NULL;
        }
        sched_yield();
        continue;
      }
      if (result != WL_OK ||
          command_view.value_size != sizeof(struct command_record)) {
        set_session_error(result == WL_OK ? WL_ERR_INVALID_STATE : result);
        return NULL;
      }
      memcpy(&command_snapshot, command_view.value,
             sizeof(command_snapshot));
      if (!command_is_consistent(&command_snapshot) ||
          command_snapshot.sequence != next_sequence) {
        set_session_error(WL_ERR_CORRUPT_PAYLOAD);
        return NULL;
      }
      borrowed_address = command_view.value;
      busy_remaining = (uint32_t)((next_sequence * UINT64_C(5)) & 3U);
      command_borrowed = true;
    }

    /*
     * This is the session's outbound submit boundary.  While it reports BUSY,
     * the oldest FIFO view is retained and retried; no command is copied,
     * released, or executed twice.
     */
    if (command_view.value != borrowed_address ||
        memcmp(command_view.value, &command_snapshot,
               sizeof(command_snapshot)) != 0) {
      set_session_error(WL_ERR_CORRUPT_PAYLOAD);
      return NULL;
    }
    result = submit_command(command_view.value, &busy_remaining);
    if (result == WL_ERR_BUSY) {
      sched_yield();
      continue;
    }
    if (result != WL_OK) {
      set_session_error(result);
      return NULL;
    }

    pending_result = (struct result_record){
        .sequence = next_sequence,
        .command_fingerprint = command_fingerprint(&command_snapshot),
        .busy_retries =
            (uint32_t)((next_sequence * UINT64_C(5)) & UINT64_C(3)),
        .status = WL_OK,
    };
    result_pending = true;
    result = wl_fifo_read_release(&command_fifo, &command_view);
    if (result != WL_OK) {
      set_session_error(result);
      return NULL;
    }
    command_borrowed = false;
    borrowed_address = NULL;
    next_sequence++;
  }
  return NULL;
}

ZTEST(wirelink_fifo_command_flow,
      test_busy_head_and_reverse_result_fifo_are_lossless) {
  uint64_t next_command = 1U;
  uint64_t next_result = 1U;
  uint32_t initial_waits = 0U;
  pthread_t session;
  wl_fifo_stats_t command_stats;
  wl_fifo_stats_t result_stats;

  memset(&command_fifo, 0, sizeof(command_fifo));
  memset(&result_fifo, 0, sizeof(result_fifo));
  memset(&command_slots, 0, sizeof(command_slots));
  memset(&result_slots, 0, sizeof(result_slots));
  atomic_init(&client_done, 0U);
  atomic_init(&result_full_seen, 0U);
  atomic_init(&session_error, WL_OK);
  zassert_ok(init_queue(&command_fifo, command_slots.bytes,
                        sizeof(command_slots.bytes),
                        sizeof(struct command_record),
                        _Alignof(struct command_record), COMMAND_CAPACITY));
  zassert_ok(init_queue(&result_fifo, result_slots.bytes,
                        sizeof(result_slots.bytes),
                        sizeof(struct result_record),
                        _Alignof(struct result_record), RESULT_CAPACITY));

  /* Start full so the session immediately exercises producer backpressure. */
  while (next_command <= COMMAND_CAPACITY) {
    zassert_ok(enqueue_command(next_command));
    next_command++;
  }
  zassert_equal(enqueue_command(next_command), WL_ERR_QUEUE_FULL);
  zassert_equal(pthread_create(&session, NULL, session_consumer, NULL), 0);

  /* Do not consume initial results until the reverse FIFO demonstrably fills. */
  while (atomic_load_explicit(&result_full_seen, memory_order_acquire) == 0U &&
         initial_waits < MAX_INITIAL_WAIT) {
    initial_waits++;
    sched_yield();
  }
  zassert_true(initial_waits < MAX_INITIAL_WAIT);

  while (next_result <= COMMAND_COUNT) {
    bool progressed = false;

    if (next_command <= COMMAND_COUNT) {
      const int result = enqueue_command(next_command);
      if (result == WL_OK) {
        next_command++;
        progressed = true;
        if (next_command > COMMAND_COUNT) {
          atomic_store_explicit(&client_done, 1U, memory_order_release);
        }
      } else {
        zassert_equal(result, WL_ERR_QUEUE_FULL);
      }
    }

    {
      wl_fifo_view_t view;
      const int result = wl_fifo_read_acquire(&result_fifo, &view);
      if (result == WL_OK) {
        const struct result_record *record = view.value;
        struct command_record expected_command;

        fill_command(&expected_command, next_result);
        zassert_equal(view.value_size, sizeof(*record));
        zassert_equal(record->sequence, next_result,
                      "result reordered, duplicated, or lost");
        zassert_equal(record->command_fingerprint,
                      command_fingerprint(&expected_command));
        zassert_equal(record->busy_retries,
                      (uint32_t)((next_result * UINT64_C(5)) & 3U));
        zassert_equal(record->status, WL_OK);
        zassert_ok(wl_fifo_read_release(&result_fifo, &view));
        next_result++;
        progressed = true;
      } else {
        zassert_equal(result, WL_ERR_NO_DATA);
      }
    }

    if ((next_result & UINT64_C(0xfff)) == 0U) {
      zassert_ok(wl_fifo_get_stats(&command_fifo, &command_stats));
      zassert_ok(wl_fifo_get_stats(&result_fifo, &result_stats));
      zassert_true(command_stats.depth <= COMMAND_CAPACITY);
      zassert_true(result_stats.depth <= RESULT_CAPACITY);
    }
    if (!progressed) {
      sched_yield();
    }
  }

  atomic_store_explicit(&client_done, 1U, memory_order_release);
  zassert_equal(pthread_join(session, NULL), 0);
  zassert_equal(atomic_load_explicit(&session_error, memory_order_relaxed),
                WL_OK);
  zassert_equal(next_command, (uint64_t)COMMAND_COUNT + 1U);
  zassert_equal(next_result, (uint64_t)COMMAND_COUNT + 1U);

  zassert_ok(wl_fifo_get_stats(&command_fifo, &command_stats));
  zassert_equal(command_stats.depth, 0U);
  zassert_equal(command_stats.publishes, COMMAND_COUNT);
  zassert_equal(command_stats.consumes, COMMAND_COUNT);
  zassert_true(command_stats.full_rejections > 0U);
  zassert_equal(command_stats.errors, 0U);

  zassert_ok(wl_fifo_get_stats(&result_fifo, &result_stats));
  zassert_equal(result_stats.depth, 0U);
  zassert_equal(result_stats.publishes, COMMAND_COUNT);
  zassert_equal(result_stats.consumes, COMMAND_COUNT);
  zassert_true(result_stats.full_rejections > 0U);
  zassert_equal(result_stats.errors, 0U);
}

ZTEST_SUITE(wirelink_fifo_command_flow, NULL, NULL, NULL, NULL, NULL);
