/* SPDX-License-Identifier: Apache-2.0 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wirelink/bulk.h"
#include "wirelink/crc.h"
#include "wirelink/frame.h"

#define DEFAULT_OBJECT_BYTES (UINT64_C(4) * UINT64_C(1024) * UINT64_C(1024))
#define MIN_OBJECT_BYTES (UINT64_C(1024) * UINT64_C(1024))
#define MAX_OBJECT_BYTES (UINT64_C(64) * UINT64_C(1024) * UINT64_C(1024))
#define DEFAULT_ITERATIONS UINT64_C(8)
#define DEFAULT_LATENCY_ITERATIONS UINT64_C(2)
#define DEFAULT_WARMUPS UINT64_C(1)
#define MAX_ITERATIONS UINT64_C(1000)
#define MAX_LATENCY_ITERATIONS UINT64_C(64)
#define MAX_LATENCY_SAMPLES UINT64_C(8000000)
#define SCHEMA_HEADROOM_BYTES 32U
#define MAX_SAFE_CHUNK_BYTES (WL_FRAME_MAX_PAYLOAD - SCHEMA_HEADROOM_BYTES)
#define CHUNK_SIZE_COUNT 4U

struct options {
  size_t object_bytes;
  uint64_t iterations;
  uint64_t latency_iterations;
  uint64_t warmups;
};

struct fixture {
  uint8_t *source;
  uint8_t *destination;
  size_t capacity;
  uint64_t next_offset;
  uint32_t transfer_id;
  uint32_t expected_crc32c;
  uint32_t finishes;
  bool active;
};

struct timing {
  uint64_t wall_ns;
  uint64_t cpu_ns;
};

struct runtime_measurement {
  struct timing elapsed;
  wl_bulk_sender_stats_t sender_stats;
  wl_bulk_receiver_stats_t receiver_stats;
};

static volatile uint64_t benchmark_sink;

static void fail_message(const char *message) {
  fprintf(stderr, "%s\n", message);
  exit(EXIT_FAILURE);
}

static void fail_bulk(const char *operation, wl_bulk_err_t result) {
  fprintf(stderr, "%s failed: %" PRId32 " (%s)\n", operation, result,
          wl_bulk_err_str(result));
  exit(EXIT_FAILURE);
}

static void require_bulk(const char *operation, wl_bulk_err_t result) {
  if (result != WL_BULK_OK) {
    fail_bulk(operation, result);
  }
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

static uint64_t checked_multiply(uint64_t left, uint64_t right,
                                 const char *name) {
  if (left != 0U && right > UINT64_MAX / left) {
    fprintf(stderr, "%s overflows uint64_t\n", name);
    exit(EXIT_FAILURE);
  }
  return left * right;
}

static uint64_t actions_per_transfer(size_t object_bytes, size_t chunk_bytes) {
  return (uint64_t)(object_bytes / chunk_bytes) +
         (object_bytes % chunk_bytes == 0U ? 0U : 1U) + UINT64_C(2);
}

static struct options parse_options(int argc, char **argv) {
  struct options options = {
      .object_bytes = (size_t)DEFAULT_OBJECT_BYTES,
      .iterations = DEFAULT_ITERATIONS,
      .latency_iterations = DEFAULT_LATENCY_ITERATIONS,
      .warmups = DEFAULT_WARMUPS,
  };
  int index;

  for (index = 1; index < argc; index += 2) {
    uint64_t parsed;

    if (index + 1 >= argc) {
      fprintf(stderr, "option has no value: %s\n", argv[index]);
      exit(EXIT_FAILURE);
    }
    parsed = parse_u64(argv[index + 1], argv[index]);
    if (strcmp(argv[index], "--bytes") == 0) {
      if (parsed > SIZE_MAX) {
        fail_message("--bytes exceeds size_t");
      }
      options.object_bytes = (size_t)parsed;
    } else if (strcmp(argv[index], "--iterations") == 0) {
      options.iterations = parsed;
    } else if (strcmp(argv[index], "--latency-iterations") == 0) {
      options.latency_iterations = parsed;
    } else if (strcmp(argv[index], "--warmups") == 0) {
      options.warmups = parsed;
    } else {
      fprintf(stderr, "unknown option: %s\n", argv[index]);
      exit(EXIT_FAILURE);
    }
  }

  if ((uint64_t)options.object_bytes < MIN_OBJECT_BYTES ||
      (uint64_t)options.object_bytes > MAX_OBJECT_BYTES) {
    fprintf(stderr, "--bytes must be %" PRIu64 "..%" PRIu64 "\n",
            MIN_OBJECT_BYTES, MAX_OBJECT_BYTES);
    exit(EXIT_FAILURE);
  }
  if (options.iterations < UINT64_C(2) || options.iterations > MAX_ITERATIONS) {
    fprintf(stderr, "--iterations must be 2..%" PRIu64 "\n", MAX_ITERATIONS);
    exit(EXIT_FAILURE);
  }
  if (options.latency_iterations == 0U ||
      options.latency_iterations > MAX_LATENCY_ITERATIONS) {
    fprintf(stderr, "--latency-iterations must be 1..%" PRIu64 "\n",
            MAX_LATENCY_ITERATIONS);
    exit(EXIT_FAILURE);
  }
  if (options.warmups > MAX_ITERATIONS) {
    fprintf(stderr, "--warmups must be 0..%" PRIu64 "\n", MAX_ITERATIONS);
    exit(EXIT_FAILURE);
  }
  if (checked_multiply(actions_per_transfer(options.object_bytes, 256U),
                       options.latency_iterations,
                       "latency sample count") > MAX_LATENCY_SAMPLES) {
    fprintf(stderr, "latency sample count must not exceed %" PRIu64 "\n",
            MAX_LATENCY_SAMPLES);
    exit(EXIT_FAILURE);
  }
  (void)checked_multiply((uint64_t)options.object_bytes, options.iterations,
                         "measured bytes");
  return options;
}

static uint64_t clock_ns(clockid_t clock_id) {
  struct timespec value;

  if (clock_gettime(clock_id, &value) != 0) {
    perror("clock_gettime");
    exit(EXIT_FAILURE);
  }
  return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
         (uint64_t)value.tv_nsec;
}

static uint64_t monotonic_ns(void) { return clock_ns(CLOCK_MONOTONIC); }

static uint64_t process_cpu_ns(void) {
  return clock_ns(CLOCK_PROCESS_CPUTIME_ID);
}

static uint8_t source_byte(size_t index) {
  uint64_t value = (uint64_t)index + UINT64_C(0x9e3779b97f4a7c15);

  value ^= value >> 30U;
  value *= UINT64_C(0xbf58476d1ce4e5b9);
  value ^= value >> 27U;
  value *= UINT64_C(0x94d049bb133111eb);
  return (uint8_t)(value ^ (value >> 31U));
}

static void fill_source(uint8_t *source, size_t length) {
  size_t index;

  for (index = 0U; index < length; ++index) {
    source[index] = source_byte(index);
  }
}

static wl_bulk_sink_result_t
fixture_begin(void *user_data, const wl_bulk_descriptor_t *descriptor,
              uint64_t *out_resume_offset) {
  struct fixture *fixture = user_data;

  if (fixture == NULL || descriptor == NULL || out_resume_offset == NULL ||
      descriptor->transfer_id == 0U ||
      descriptor->total_length != fixture->capacity ||
      descriptor->object_crc32c != fixture->expected_crc32c) {
    return WL_BULK_SINK_INVALID;
  }
  fixture->next_offset = 0U;
  fixture->transfer_id = descriptor->transfer_id;
  fixture->active = true;
  *out_resume_offset = 0U;
  return WL_BULK_SINK_OK;
}

static wl_bulk_sink_result_t fixture_write(void *user_data,
                                           uint32_t transfer_id,
                                           uint64_t offset, const uint8_t *data,
                                           size_t length) {
  struct fixture *fixture = user_data;

  if (fixture == NULL || !fixture->active || data == NULL || length == 0U ||
      transfer_id != fixture->transfer_id || offset != fixture->next_offset ||
      offset > fixture->capacity ||
      length > fixture->capacity - (size_t)offset) {
    return WL_BULK_SINK_INVALID;
  }
  memcpy(fixture->destination + (size_t)offset, data, length);
  fixture->next_offset += (uint64_t)length;
  return WL_BULK_SINK_OK;
}

static wl_bulk_sink_result_t
fixture_finish(void *user_data, const wl_bulk_descriptor_t *descriptor) {
  struct fixture *fixture = user_data;
  uint32_t crc32c;

  if (fixture == NULL || descriptor == NULL || !fixture->active ||
      descriptor->transfer_id != fixture->transfer_id ||
      descriptor->total_length != fixture->next_offset) {
    return WL_BULK_SINK_INVALID;
  }
  crc32c = wl_crc32c(fixture->destination, fixture->capacity);
  benchmark_sink =
      benchmark_sink * UINT64_C(33) + (uint64_t)crc32c + fixture->finishes;
  if (crc32c != descriptor->object_crc32c) {
    return WL_BULK_SINK_INTEGRITY_FAILED;
  }
  fixture->active = false;
  ++fixture->finishes;
  return WL_BULK_SINK_OK;
}

static void fixture_abort(void *user_data, uint32_t transfer_id,
                          int32_t reason) {
  struct fixture *fixture = user_data;

  if (fixture != NULL && fixture->active &&
      transfer_id == fixture->transfer_id) {
    fixture->active = false;
    benchmark_sink = benchmark_sink * UINT64_C(33) + (uint64_t)(uint32_t)reason;
  }
}

static void init_runtime(struct fixture *fixture, size_t chunk_bytes,
                         wl_bulk_sender_t *sender,
                         wl_bulk_receiver_t *receiver) {
  const wl_bulk_sender_config_t sender_config = {
      .status_timeout_ms = 20U,
      .busy_retry_ms = 1U,
      .max_retries = 2U,
  };
  const wl_bulk_receiver_config_t receiver_config = {
      .max_object_length = fixture->capacity,
      .max_chunk_size = (uint32_t)chunk_bytes,
      .write_alignment = 1U,
      .idle_timeout_ms = 0U,
      .sink =
          {
              .user_data = fixture,
              .begin = fixture_begin,
              .write = fixture_write,
              .finish = fixture_finish,
              .abort = fixture_abort,
          },
  };

  require_bulk("wl_bulk_sender_init",
               wl_bulk_sender_init(sender, &sender_config));
  require_bulk("wl_bulk_receiver_init",
               wl_bulk_receiver_init(receiver, &receiver_config));
  fixture->active = false;
  fixture->next_offset = 0U;
  fixture->transfer_id = 0U;
  fixture->finishes = 0U;
}

static void deliver_action(wl_bulk_sender_t *sender,
                           wl_bulk_receiver_t *receiver,
                           const struct fixture *fixture,
                           const wl_bulk_sender_action_t *action,
                           wl_time_ms_t now_ms) {
  wl_bulk_receiver_status_view_t status_view;
  wl_bulk_err_t result;

  require_bulk("wl_bulk_sender_action_submitted",
               wl_bulk_sender_action_submitted(sender, action, now_ms));
  switch (action->phase) {
  case WL_BULK_PHASE_BEGIN:
    result = wl_bulk_receiver_on_begin(receiver, &action->descriptor, now_ms);
    break;
  case WL_BULK_PHASE_CHUNK: {
    const wl_bulk_chunk_t chunk = {
        .transfer_id = action->descriptor.transfer_id,
        .offset = action->offset,
        .data = fixture->source + (size_t)action->offset,
        .length = action->length,
    };
    result = wl_bulk_receiver_on_chunk(receiver, &chunk, now_ms);
    break;
  }
  case WL_BULK_PHASE_END:
    result = wl_bulk_receiver_on_end(receiver, action->descriptor.transfer_id,
                                     action->descriptor.total_length,
                                     action->descriptor.object_crc32c, now_ms);
    break;
  case WL_BULK_PHASE_ABORT:
    result = wl_bulk_receiver_on_abort(receiver, action->descriptor.transfer_id,
                                       action->abort_reason, now_ms);
    break;
  default:
    fail_message("sender produced an invalid bulk action phase");
    return;
  }
  require_bulk("bulk receiver action", result);
  require_bulk("wl_bulk_receiver_status_acquire",
               wl_bulk_receiver_status_acquire(receiver, &status_view));
  require_bulk("wl_bulk_sender_on_status",
               wl_bulk_sender_on_status(sender, &status_view.status, now_ms));
  require_bulk("wl_bulk_receiver_status_release",
               wl_bulk_receiver_status_release(receiver, &status_view));
}

static uint64_t run_transfer(struct fixture *fixture, size_t chunk_bytes,
                             wl_bulk_sender_t *sender,
                             wl_bulk_receiver_t *receiver,
                             uint64_t *latency_samples,
                             uint64_t latency_capacity, uint64_t *latency_count,
                             uint64_t iteration) {
  const wl_bulk_descriptor_t descriptor = {
      .transfer_id = UINT32_C(0x42554c4b),
      .total_length = fixture->capacity,
      .requested_chunk_size = (uint32_t)chunk_bytes,
      .object_crc32c = fixture->expected_crc32c,
  };
  const uint64_t action_limit =
      actions_per_transfer(fixture->capacity, chunk_bytes);
  uint64_t action_count = 0U;

  if (iteration != 0U) {
    require_bulk("wl_bulk_sender_reset", wl_bulk_sender_reset(sender));
    require_bulk("wl_bulk_receiver_reset", wl_bulk_receiver_reset(receiver));
  }
  require_bulk("wl_bulk_sender_start",
               wl_bulk_sender_start(sender, &descriptor));

  for (;;) {
    wl_bulk_sender_result_t sender_result;
    wl_bulk_sender_action_t action;
    uint64_t started_ns = 0U;
    uint64_t finished_ns;
    const wl_time_ms_t now_ms = (wl_time_ms_t)(iteration + action_count);

    require_bulk("wl_bulk_sender_get_result",
                 wl_bulk_sender_get_result(sender, &sender_result));
    if (sender_result.state == WL_BULK_SENDER_COMPLETED) {
      break;
    }
    if (sender_result.state == WL_BULK_SENDER_FAILED ||
        sender_result.state == WL_BULK_SENDER_ABORTED) {
      fail_message("bulk sender terminated without completing");
    }
    if (action_count >= action_limit) {
      fail_message("bulk transfer exceeded its exact action bound");
    }
    require_bulk("wl_bulk_sender_action_acquire",
                 wl_bulk_sender_action_acquire(sender, &action));
    if (latency_samples != NULL) {
      if (*latency_count >= latency_capacity) {
        fail_message("latency sample storage exhausted");
      }
      started_ns = monotonic_ns();
    }
    deliver_action(sender, receiver, fixture, &action, now_ms);
    if (latency_samples != NULL) {
      finished_ns = monotonic_ns();
      latency_samples[*latency_count] = finished_ns - started_ns;
      ++*latency_count;
    }
    ++action_count;
  }
  if (action_count != action_limit) {
    fail_message("bulk transfer action count did not match the expected count");
  }
  return action_count;
}

static void validate_destination(const struct fixture *fixture) {
  if (fixture->active || fixture->next_offset != fixture->capacity ||
      memcmp(fixture->source, fixture->destination, fixture->capacity) != 0 ||
      wl_crc32c(fixture->destination, fixture->capacity) !=
          fixture->expected_crc32c) {
    fail_message("bulk benchmark destination validation failed");
  }
}

static void run_raw_work(struct fixture *fixture, size_t chunk_bytes,
                         uint64_t iterations) {
  uint64_t iteration;

  for (iteration = 0U; iteration < iterations; ++iteration) {
    size_t offset = 0U;
    uint32_t crc32c;

    while (offset < fixture->capacity) {
      const size_t remaining = fixture->capacity - offset;
      const size_t length = remaining < chunk_bytes ? remaining : chunk_bytes;

      memcpy(fixture->destination + offset, fixture->source + offset, length);
      offset += length;
    }
    crc32c = wl_crc32c(fixture->destination, fixture->capacity);
    benchmark_sink =
        benchmark_sink * UINT64_C(33) + (uint64_t)crc32c + iteration;
    if (crc32c != fixture->expected_crc32c) {
      fail_message("raw sequential sink CRC validation failed");
    }
  }
  fixture->next_offset = fixture->capacity;
  fixture->active = false;
}

static struct timing measure_raw(struct fixture *fixture, size_t chunk_bytes,
                                 uint64_t iterations) {
  struct timing timing;
  uint64_t wall_start;
  uint64_t cpu_start;

  cpu_start = process_cpu_ns();
  wall_start = monotonic_ns();
  run_raw_work(fixture, chunk_bytes, iterations);
  timing.wall_ns = monotonic_ns() - wall_start;
  timing.cpu_ns = process_cpu_ns() - cpu_start;
  validate_destination(fixture);
  return timing;
}

static void run_runtime_work(struct fixture *fixture, size_t chunk_bytes,
                             uint64_t iterations, wl_bulk_sender_t *sender,
                             wl_bulk_receiver_t *receiver) {
  uint64_t iteration;

  for (iteration = 0U; iteration < iterations; ++iteration) {
    (void)run_transfer(fixture, chunk_bytes, sender, receiver, NULL, 0U, NULL,
                       iteration);
  }
}

static struct runtime_measurement measure_runtime(struct fixture *fixture,
                                                  size_t chunk_bytes,
                                                  uint64_t iterations) {
  struct runtime_measurement measurement;
  wl_bulk_sender_t sender;
  wl_bulk_receiver_t receiver;
  uint64_t wall_start;
  uint64_t cpu_start;

  init_runtime(fixture, chunk_bytes, &sender, &receiver);
  cpu_start = process_cpu_ns();
  wall_start = monotonic_ns();
  run_runtime_work(fixture, chunk_bytes, iterations, &sender, &receiver);
  measurement.elapsed.wall_ns = monotonic_ns() - wall_start;
  measurement.elapsed.cpu_ns = process_cpu_ns() - cpu_start;
  validate_destination(fixture);
  require_bulk("wl_bulk_sender_get_stats",
               wl_bulk_sender_get_stats(&sender, &measurement.sender_stats));
  require_bulk(
      "wl_bulk_receiver_get_stats",
      wl_bulk_receiver_get_stats(&receiver, &measurement.receiver_stats));
  return measurement;
}

static int compare_u64(const void *left, const void *right) {
  const uint64_t left_value = *(const uint64_t *)left;
  const uint64_t right_value = *(const uint64_t *)right;

  return left_value < right_value ? -1 : left_value > right_value ? 1 : 0;
}

static uint64_t percentile(const uint64_t *samples, uint64_t count,
                           uint32_t percentile_value) {
  uint64_t rank;

  if (count == 0U || percentile_value == 0U || percentile_value > 100U) {
    fail_message("invalid percentile input");
  }
  rank = (checked_multiply(count, percentile_value, "percentile rank") +
          UINT64_C(99)) /
         UINT64_C(100);
  return samples[rank - 1U];
}

static uint64_t measure_latency(struct fixture *fixture, size_t chunk_bytes,
                                uint64_t iterations, uint64_t *samples,
                                uint64_t capacity) {
  wl_bulk_sender_t sender;
  wl_bulk_receiver_t receiver;
  uint64_t count = 0U;
  uint64_t iteration;

  init_runtime(fixture, chunk_bytes, &sender, &receiver);
  for (iteration = 0U; iteration < iterations; ++iteration) {
    (void)run_transfer(fixture, chunk_bytes, &sender, &receiver, samples,
                       capacity, &count, iteration);
  }
  validate_destination(fixture);
  qsort(samples, (size_t)count, sizeof(samples[0]), compare_u64);
  return count;
}

static double goodput_mib_per_second(uint64_t bytes, uint64_t elapsed_ns) {
  return (double)bytes * 1000000000.0 / ((double)elapsed_ns * 1024.0 * 1024.0);
}

static double cpu_ns_per_byte(uint64_t cpu_ns, uint64_t bytes) {
  return (double)cpu_ns / (double)bytes;
}

static void run_chunk_benchmark(struct fixture *fixture,
                                const struct options *options,
                                size_t chunk_bytes, uint64_t *latency_samples,
                                uint64_t latency_capacity) {
  struct runtime_measurement runtime;
  struct timing raw;
  uint64_t latency_count;
  const uint64_t measured_bytes = checked_multiply(
      (uint64_t)fixture->capacity, options->iterations, "measured bytes");
  double raw_goodput;
  double runtime_goodput;
  double raw_cpu;
  double runtime_cpu;
  double goodput_ratio;
  double cpu_overhead;

  if (options->warmups != 0U) {
    wl_bulk_sender_t sender;
    wl_bulk_receiver_t receiver;

    run_raw_work(fixture, chunk_bytes, options->warmups);
    init_runtime(fixture, chunk_bytes, &sender, &receiver);
    run_runtime_work(fixture, chunk_bytes, options->warmups, &sender,
                     &receiver);
    validate_destination(fixture);
  }

  raw = measure_raw(fixture, chunk_bytes, options->iterations);
  runtime = measure_runtime(fixture, chunk_bytes, options->iterations);
  latency_count =
      measure_latency(fixture, chunk_bytes, options->latency_iterations,
                      latency_samples, latency_capacity);

  raw_goodput = goodput_mib_per_second(measured_bytes, raw.wall_ns);
  runtime_goodput =
      goodput_mib_per_second(measured_bytes, runtime.elapsed.wall_ns);
  raw_cpu = cpu_ns_per_byte(raw.cpu_ns, measured_bytes);
  runtime_cpu = cpu_ns_per_byte(runtime.elapsed.cpu_ns, measured_bytes);
  goodput_ratio = runtime_goodput * 100.0 / raw_goodput;
  cpu_overhead = (runtime_cpu - raw_cpu) * 100.0 / raw_cpu;

  printf("bulk chunk_bytes=%zu object_bytes=%zu iterations=%" PRIu64
         " raw_goodput_mib_s=%.2f runtime_goodput_mib_s=%.2f"
         " goodput_ratio_pct=%.2f raw_cpu_ns_per_byte=%.4f"
         " runtime_cpu_ns_per_byte=%.4f cpu_overhead_pct=%.2f"
         " status_state_samples=%" PRIu64 " status_state_p50_ns=%" PRIu64
         " status_state_p99_ns=%" PRIu64 " sender_retries=%" PRIu32
         " sender_busy=%" PRIu32 " receiver_busy=%" PRIu32
         " goodput_target_90pct=%s cpu_target_10pct=%s"
         " verified_crc32c=0x%08" PRIx32 "\n",
         chunk_bytes, fixture->capacity, options->iterations, raw_goodput,
         runtime_goodput, goodput_ratio, raw_cpu, runtime_cpu, cpu_overhead,
         latency_count, percentile(latency_samples, latency_count, 50U),
         percentile(latency_samples, latency_count, 99U),
         runtime.sender_stats.retries, runtime.sender_stats.busy_responses,
         runtime.receiver_stats.busy_responses,
         goodput_ratio >= 90.0 ? "met" : "missed",
         cpu_overhead <= 10.0 ? "met" : "missed", fixture->expected_crc32c);
}

int main(int argc, char **argv) {
  static const size_t chunk_sizes[CHUNK_SIZE_COUNT] = {
      256U,
      512U,
      1024U,
      MAX_SAFE_CHUNK_BYTES,
  };
  const struct options options = parse_options(argc, argv);
  const uint64_t latency_capacity = checked_multiply(
      actions_per_transfer(options.object_bytes, chunk_sizes[0]),
      options.latency_iterations, "latency sample count");
  struct fixture fixture;
  uint64_t *latency_samples;
  size_t chunk_index;

  if (MAX_SAFE_CHUNK_BYTES <= 1024U || MAX_SAFE_CHUNK_BYTES > UINT32_MAX) {
    fail_message("invalid conservative maximum-safe chunk size");
  }
  fixture.source = malloc(options.object_bytes);
  fixture.destination = malloc(options.object_bytes);
  latency_samples =
      malloc((size_t)latency_capacity * sizeof(latency_samples[0]));
  if (fixture.source == NULL || fixture.destination == NULL ||
      latency_samples == NULL) {
    fail_message("benchmark startup allocation failed");
  }
  fixture.capacity = options.object_bytes;
  fixture.next_offset = 0U;
  fixture.transfer_id = 0U;
  fixture.finishes = 0U;
  fixture.active = false;
  fill_source(fixture.source, fixture.capacity);
  memset(fixture.destination, 0, fixture.capacity);
  fixture.expected_crc32c = wl_crc32c(fixture.source, fixture.capacity);

  printf("bulk_benchmark object_bytes=%zu iterations=%" PRIu64
         " latency_iterations=%" PRIu64 " warmups=%" PRIu64
         " max_safe_chunk_bytes=%u schema_headroom_bytes=%u\n",
         options.object_bytes, options.iterations, options.latency_iterations,
         options.warmups, (unsigned int)MAX_SAFE_CHUNK_BYTES,
         (unsigned int)SCHEMA_HEADROOM_BYTES);
  for (chunk_index = 0U; chunk_index < CHUNK_SIZE_COUNT; ++chunk_index) {
    run_chunk_benchmark(&fixture, &options, chunk_sizes[chunk_index],
                        latency_samples, latency_capacity);
  }
  printf("benchmark_sink=0x%016" PRIx64 "\n", benchmark_sink);

  free(latency_samples);
  free(fixture.destination);
  free(fixture.source);
  return EXIT_SUCCESS;
}
