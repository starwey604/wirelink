/* SPDX-License-Identifier: Apache-2.0 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wirelink/fifo.h"

#define DEFAULT_HANDOFFS UINT64_C(1000000)
#define DEFAULT_COMMANDS UINT64_C(250000)
#define DEFAULT_CAPACITY UINT32_C(64)
#define MAX_CAPACITY UINT32_C(4096)
#define MAX_COMMANDS UINT64_C(1000000)
#define SLOT_BYTES 64U

struct options {
  uint64_t handoffs;
  uint64_t commands;
  uint32_t capacity;
};

struct bench_record {
  uint64_t sequence;
  uint64_t inverse;
  uint64_t enqueue_ns;
  uint64_t lanes[3];
};

struct result_record {
  uint64_t sequence;
  uint64_t fingerprint;
  uint64_t enqueue_to_submit_ns;
  uint32_t busy_retries;
  int32_t status;
};

union slot_storage {
  max_align_t align;
  uint8_t bytes[MAX_CAPACITY * SLOT_BYTES];
};

struct command_flow {
  wl_fifo_t commands;
  wl_fifo_t results;
  union slot_storage command_slots;
  union slot_storage result_slots;
  uint64_t command_count;
  _Atomic uint32_t producer_done;
  _Atomic int error;
};

static wl_fifo_t handoff_fifo;
static union slot_storage handoff_slots;
static struct command_flow flow;
static uint64_t latency_samples[MAX_COMMANDS];
static volatile uint64_t benchmark_sink;

static void fail(const char *operation, int result) {
  fprintf(stderr, "%s failed: %d\n", operation, result);
  exit(EXIT_FAILURE);
}

static uint64_t parse_u64(const char *text, const char *name) {
  char *end = NULL;
  unsigned long long parsed;

  errno = 0;
  parsed = strtoull(text, &end, 0);
  if (errno != 0 || end == text || *end != '\0') {
    fprintf(stderr, "invalid %s: %s\n", name, text);
    exit(EXIT_FAILURE);
  }
  return (uint64_t)parsed;
}

static struct options parse_options(int argc, char **argv) {
  struct options options = {
      .handoffs = DEFAULT_HANDOFFS,
      .commands = DEFAULT_COMMANDS,
      .capacity = DEFAULT_CAPACITY,
  };
  int index;

  for (index = 1; index < argc; index += 2) {
    if (index + 1 >= argc) {
      fprintf(stderr, "option has no value: %s\n", argv[index]);
      exit(EXIT_FAILURE);
    }
    if (strcmp(argv[index], "--handoffs") == 0) {
      options.handoffs = parse_u64(argv[index + 1], "handoffs");
    } else if (strcmp(argv[index], "--commands") == 0) {
      options.commands = parse_u64(argv[index + 1], "commands");
    } else if (strcmp(argv[index], "--capacity") == 0) {
      const uint64_t capacity = parse_u64(argv[index + 1], "capacity");
      if (capacity > UINT32_MAX) {
        fprintf(stderr, "capacity exceeds uint32_t\n");
        exit(EXIT_FAILURE);
      }
      options.capacity = (uint32_t)capacity;
    } else {
      fprintf(stderr, "unknown option: %s\n", argv[index]);
      exit(EXIT_FAILURE);
    }
  }
  if (options.handoffs == 0U || options.handoffs > UINT32_MAX ||
      options.commands == 0U ||
      options.commands > MAX_COMMANDS || options.capacity < 2U ||
      options.capacity > MAX_CAPACITY) {
    fprintf(stderr,
            "handoffs must be 1..%" PRIu32 "; commands must be 1..%" PRIu64
            "; capacity must be 2..%" PRIu32 "\n",
            UINT32_MAX, MAX_COMMANDS, MAX_CAPACITY);
    exit(EXIT_FAILURE);
  }
  return options;
}

static uint64_t monotonic_ns(void) {
  struct timespec time;

  if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
    perror("clock_gettime");
    exit(EXIT_FAILURE);
  }
  return (uint64_t)time.tv_sec * UINT64_C(1000000000) +
         (uint64_t)time.tv_nsec;
}

static uint64_t mix64(uint64_t value) {
  value ^= value >> 30U;
  value *= UINT64_C(0xbf58476d1ce4e5b9);
  value ^= value >> 27U;
  value *= UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

static void fill_record(struct bench_record *record, uint64_t sequence,
                        uint64_t enqueue_ns) {
  size_t index;

  record->sequence = sequence;
  record->inverse = ~sequence;
  record->enqueue_ns = enqueue_ns;
  for (index = 0U; index < sizeof(record->lanes) / sizeof(record->lanes[0]);
       index++) {
    record->lanes[index] = mix64(sequence + index);
  }
}

static bool record_is_consistent(const struct bench_record *record) {
  struct bench_record expected;

  fill_record(&expected, record->sequence, record->enqueue_ns);
  return memcmp(record, &expected, sizeof(expected)) == 0;
}

static uint64_t record_fingerprint(const struct bench_record *record) {
  return record->sequence ^ record->lanes[0] ^ record->lanes[1] ^
         record->lanes[2];
}

static void init_queue(wl_fifo_t *fifo, union slot_storage *storage,
                       size_t value_size, size_t value_alignment,
                       uint32_t capacity) {
  const wl_fifo_config_t config = {
      .value_size = value_size,
      .value_alignment = value_alignment,
      .capacity = capacity,
  };
  const wl_fifo_storage_t fifo_storage = {
      .data = storage->bytes,
      .size = sizeof(storage->bytes),
  };
  wl_fifo_requirements_t requirements;
  int result;

  memset(fifo, 0, sizeof(*fifo));
  memset(storage, 0, sizeof(*storage));
  result = wl_fifo_requirements(&config, &requirements);
  if (result != WL_OK) {
    fail("wl_fifo_requirements", result);
  }
  if (requirements.storage_size > sizeof(storage->bytes) ||
      requirements.slot_stride > SLOT_BYTES) {
    fail("FIFO benchmark storage", WL_ERR_BUF_TOO_SMALL);
  }
  result = wl_fifo_init(fifo, &config, &fifo_storage);
  if (result != WL_OK) {
    fail("wl_fifo_init", result);
  }
}

static double ns_per_operation(uint64_t elapsed_ns, uint64_t operations) {
  return (double)elapsed_ns / (double)operations;
}

static double operations_per_second(uint64_t elapsed_ns,
                                    uint64_t operations) {
  return (double)operations * 1000000000.0 / (double)elapsed_ns;
}

static void handoff_one(uint64_t sequence) {
  wl_fifo_write_claim_t claim;
  wl_fifo_view_t view;
  const struct bench_record *record;
  int result = wl_fifo_write_claim(&handoff_fifo, &claim);

  if (result != WL_OK) {
    fail("handoff write claim", result);
  }
  fill_record(claim.value, sequence, 0U);
  result = wl_fifo_write_publish(&handoff_fifo, &claim);
  if (result != WL_OK) {
    fail("handoff publish", result);
  }
  result = wl_fifo_read_acquire(&handoff_fifo, &view);
  if (result != WL_OK) {
    fail("handoff read acquire", result);
  }
  record = view.value;
  if (!record_is_consistent(record) || record->sequence != sequence) {
    fail("handoff validation", WL_ERR_CORRUPT_PAYLOAD);
  }
  benchmark_sink ^= record_fingerprint(record);
  result = wl_fifo_read_release(&handoff_fifo, &view);
  if (result != WL_OK) {
    fail("handoff read release", result);
  }
}

static void run_handoff_benchmark(const struct options *options) {
  const uint64_t warmups =
      options->handoffs < UINT64_C(50000) ? options->handoffs
                                          : UINT64_C(50000);
  uint64_t sequence;
  uint64_t start;
  uint64_t elapsed;
  wl_fifo_stats_t stats;

  init_queue(&handoff_fifo, &handoff_slots, sizeof(struct bench_record),
             _Alignof(struct bench_record), options->capacity);
  for (sequence = 1U; sequence <= warmups; sequence++) {
    handoff_one(sequence);
  }
  init_queue(&handoff_fifo, &handoff_slots, sizeof(struct bench_record),
             _Alignof(struct bench_record), options->capacity);
  start = monotonic_ns();
  for (sequence = 1U; sequence <= options->handoffs; sequence++) {
    handoff_one(sequence);
  }
  elapsed = monotonic_ns() - start;
  if (wl_fifo_get_stats(&handoff_fifo, &stats) != WL_OK || stats.depth != 0U ||
      stats.publishes != options->handoffs ||
      stats.consumes != options->handoffs || stats.errors != 0U) {
    fail("handoff statistics", WL_ERR_INVALID_STATE);
  }
  printf("handoff records=%" PRIu64 " ops_per_second=%.2f ns_per_op=%.2f\n",
         options->handoffs,
         operations_per_second(elapsed, options->handoffs),
         ns_per_operation(elapsed, options->handoffs));
}

static void run_stats_benchmark(const struct options *options) {
  uint64_t query;
  uint64_t start;
  uint64_t elapsed;
  wl_fifo_stats_t stats;

  start = monotonic_ns();
  for (query = 0U; query < options->handoffs; query++) {
    const int result = wl_fifo_get_stats(&handoff_fifo, &stats);
    if (result != WL_OK) {
      fail("stats snapshot", result);
    }
    benchmark_sink += stats.depth + stats.publishes + stats.consumes;
  }
  elapsed = monotonic_ns() - start;
  printf("stats snapshots=%" PRIu64
         " snapshots_per_second=%.2f ns_per_snapshot=%.2f\n",
         options->handoffs,
         operations_per_second(elapsed, options->handoffs),
         ns_per_operation(elapsed, options->handoffs));
}

static void set_flow_error(struct command_flow *state, int error) {
  int expected = WL_OK;

  (void)atomic_compare_exchange_strong_explicit(
      &state->error, &expected, error, memory_order_relaxed,
      memory_order_relaxed);
}

static int publish_flow_result(struct command_flow *state,
                               const struct result_record *result) {
  wl_fifo_write_claim_t claim;
  int status = wl_fifo_write_claim(&state->results, &claim);

  if (status != WL_OK) {
    return status;
  }
  memcpy(claim.value, result, sizeof(*result));
  return wl_fifo_write_publish(&state->results, &claim);
}

static void *command_consumer(void *argument) {
  struct command_flow *state = argument;
  wl_fifo_view_t command_view = {0};
  struct result_record pending_result = {0};
  uint64_t next_sequence = 1U;
  uint32_t busy_remaining = 0U;
  bool command_borrowed = false;
  bool result_pending = false;

  while (next_sequence <= state->command_count || result_pending) {
    int result;

    if (result_pending) {
      result = publish_flow_result(state, &pending_result);
      if (result == WL_ERR_QUEUE_FULL) {
        sched_yield();
        continue;
      }
      if (result != WL_OK) {
        set_flow_error(state, result);
        return NULL;
      }
      result_pending = false;
      if (next_sequence > state->command_count) {
        break;
      }
    }

    if (!command_borrowed) {
      result = wl_fifo_read_acquire(&state->commands, &command_view);
      if (result == WL_ERR_NO_DATA) {
        if (atomic_load_explicit(&state->producer_done,
                                 memory_order_acquire) != 0U &&
            next_sequence <= state->command_count) {
          set_flow_error(state, WL_ERR_NO_DATA);
          return NULL;
        }
        sched_yield();
        continue;
      }
      if (result != WL_OK ||
          command_view.value_size != sizeof(struct bench_record) ||
          !record_is_consistent(command_view.value) ||
          ((const struct bench_record *)command_view.value)->sequence !=
              next_sequence) {
        set_flow_error(state,
                       result == WL_OK ? WL_ERR_CORRUPT_PAYLOAD : result);
        return NULL;
      }
      busy_remaining = ((next_sequence & UINT64_C(7)) == 0U) ? 1U : 0U;
      command_borrowed = true;
    }

    if (busy_remaining != 0U) {
      busy_remaining--;
      sched_yield();
      continue;
    }

    {
      const struct bench_record *record = command_view.value;
      const uint64_t accepted_ns = monotonic_ns();

      pending_result = (struct result_record){
          .sequence = next_sequence,
          .fingerprint = record_fingerprint(record),
          .enqueue_to_submit_ns = accepted_ns - record->enqueue_ns,
          .busy_retries =
              ((next_sequence & UINT64_C(7)) == 0U) ? 1U : 0U,
          .status = WL_OK,
      };
    }
    result_pending = true;
    result = wl_fifo_read_release(&state->commands, &command_view);
    if (result != WL_OK) {
      set_flow_error(state, result);
      return NULL;
    }
    command_borrowed = false;
    next_sequence++;
  }
  return NULL;
}

static int compare_u64(const void *left, const void *right) {
  const uint64_t first = *(const uint64_t *)left;
  const uint64_t second = *(const uint64_t *)right;

  return first > second ? 1 : (first < second ? -1 : 0);
}

static uint64_t percentile(const uint64_t *sorted, uint64_t count,
                           uint32_t numerator) {
  const uint64_t index = ((count - 1U) * numerator) / 100U;
  return sorted[index];
}

static void run_command_benchmark(const struct options *options) {
  uint64_t next_command = 1U;
  uint64_t next_result = 1U;
  uint64_t start;
  uint64_t elapsed;
  pthread_t consumer;
  wl_fifo_stats_t command_stats;
  wl_fifo_stats_t result_stats;

  memset(&flow, 0, sizeof(flow));
  flow.command_count = options->commands;
  atomic_init(&flow.producer_done, 0U);
  atomic_init(&flow.error, WL_OK);
  init_queue(&flow.commands, &flow.command_slots, sizeof(struct bench_record),
             _Alignof(struct bench_record), options->capacity);
  init_queue(&flow.results, &flow.result_slots, sizeof(struct result_record),
             _Alignof(struct result_record), options->capacity);
  memset(latency_samples, 0,
         (size_t)options->commands * sizeof(latency_samples[0]));

  start = monotonic_ns();
  if (pthread_create(&consumer, NULL, command_consumer, &flow) != 0) {
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }
  while (next_result <= options->commands) {
    bool progressed = false;

    if (next_command <= options->commands) {
      wl_fifo_write_claim_t claim;
      const int result = wl_fifo_write_claim(&flow.commands, &claim);

      if (result == WL_OK) {
        fill_record(claim.value, next_command, monotonic_ns());
        if (wl_fifo_write_publish(&flow.commands, &claim) != WL_OK) {
          fail("command publish", WL_ERR_INVALID_STATE);
        }
        next_command++;
        progressed = true;
        if (next_command > options->commands) {
          atomic_store_explicit(&flow.producer_done, 1U,
                                memory_order_release);
        }
      } else if (result != WL_ERR_QUEUE_FULL) {
        fail("command claim", result);
      }
    }

    {
      wl_fifo_view_t view;
      const int result = wl_fifo_read_acquire(&flow.results, &view);

      if (result == WL_OK) {
        const struct result_record *record = view.value;
        struct bench_record expected;

        fill_record(&expected, next_result, 0U);
        if (record->sequence != next_result || record->status != WL_OK ||
            record->fingerprint != record_fingerprint(&expected)) {
          fail("command result validation", WL_ERR_CORRUPT_PAYLOAD);
        }
        latency_samples[next_result - 1U] = record->enqueue_to_submit_ns;
        benchmark_sink ^= record->fingerprint;
        if (wl_fifo_read_release(&flow.results, &view) != WL_OK) {
          fail("result release", WL_ERR_INVALID_STATE);
        }
        next_result++;
        progressed = true;
      } else if (result != WL_ERR_NO_DATA) {
        fail("result acquire", result);
      }
    }
    if (!progressed) {
      sched_yield();
    }
  }
  atomic_store_explicit(&flow.producer_done, 1U, memory_order_release);
  if (pthread_join(consumer, NULL) != 0) {
    perror("pthread_join");
    exit(EXIT_FAILURE);
  }
  elapsed = monotonic_ns() - start;
  if (atomic_load_explicit(&flow.error, memory_order_relaxed) != WL_OK) {
    fail("command consumer", atomic_load_explicit(&flow.error,
                                                   memory_order_relaxed));
  }
  if (wl_fifo_get_stats(&flow.commands, &command_stats) != WL_OK ||
      wl_fifo_get_stats(&flow.results, &result_stats) != WL_OK ||
      command_stats.depth != 0U || result_stats.depth != 0U ||
      command_stats.publishes != options->commands ||
      command_stats.consumes != options->commands ||
      result_stats.publishes != options->commands ||
      result_stats.consumes != options->commands ||
      command_stats.errors != 0U || result_stats.errors != 0U) {
    fail("command statistics", WL_ERR_INVALID_STATE);
  }

  qsort(latency_samples, (size_t)options->commands,
        sizeof(latency_samples[0]), compare_u64);
  printf("command_flow records=%" PRIu64
         " ops_per_second=%.2f ns_per_op=%.2f command_full=%" PRIu32
         " result_full=%" PRIu32 "\n",
         options->commands,
         operations_per_second(elapsed, options->commands),
         ns_per_operation(elapsed, options->commands),
         command_stats.full_rejections, result_stats.full_rejections);
  printf("enqueue_to_submit_ns p50=%" PRIu64 " p99=%" PRIu64
         " max=%" PRIu64 "\n",
         percentile(latency_samples, options->commands, 50U),
         percentile(latency_samples, options->commands, 99U),
         latency_samples[options->commands - 1U]);
}

int main(int argc, char **argv) {
  const struct options options = parse_options(argc, argv);

  printf("wirelink_fifo_benchmark_v1 capacity=%" PRIu32
         " record_bytes=%zu\n",
         options.capacity, sizeof(struct bench_record));
  run_handoff_benchmark(&options);
  run_stats_benchmark(&options);
  run_command_benchmark(&options);
  printf("benchmark_sink=%" PRIu64 "\n", benchmark_sink);
  return EXIT_SUCCESS;
}
