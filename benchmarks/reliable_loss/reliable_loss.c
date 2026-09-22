/* SPDX-License-Identifier: Apache-2.0 */

/* Reliable-path performance under packet loss.
 *
 * Two Wirelink links exchange one reliable transaction at a time through a
 * seeded lossy channel. The links run their real retransmission, ACK, duplicate
 * suppression and retry-exhaustion logic; only the channel is synthetic: a unit
 * is dropped with probability p, otherwise delivered after a fixed one-way
 * delay. A simulated millisecond clock advances one step per iteration, so a
 * run is fully deterministic for a given seed and the convergence time is a
 * protocol time, not a wall-clock sample.
 *
 * This complements tests/tutorials/udp_processes.py (real UDP, correctness) and
 * mixed_traffic (application-level ACK loss): it isolates the link retry path
 * and reports its convergence distribution and attempt cost. */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wirelink/frame.h"
#include "wirelink/wirelink.h"

#define CHANNEL_CAPACITY 64U
#define MAX_LOSS_CASES 32U
#define RNG_SEED UINT64_C(0x9E3779B97F4A7C15)
#define MESSAGE_ID 0x1E00U

typedef struct {
  wl_ctx_t ctx;
  wl_config_t config;
  wl_storage_t storage;
  uint8_t tx_payload[WL_FRAME_MAX_PAYLOAD];
  uint8_t tx_unit[WL_FRAME_MAX_COBS_LEN];
  uint8_t control_unit[WL_FRAME_MAX_COBS_LEN];
  uint8_t rx_fifo[WL_FRAME_MAX_COBS_LEN];
  uint8_t rx_fallback[WL_FRAME_MAX_COBS_LEN];
} link_t;

typedef struct {
  uint8_t data[CHANNEL_CAPACITY][WL_FRAME_MAX_COBS_LEN];
  size_t length[CHANNEL_CAPACITY];
  uint32_t deliver_at[CHANNEL_CAPACITY];
  uint8_t active[CHANNEL_CAPACITY];
  uint64_t rng;
  uint32_t loss_ppm;
  uint32_t delay_ms;
  uint32_t now_ms;
  uint64_t enqueued;
  uint64_t dropped;
  uint64_t feed_errors;
} channel_t;

typedef struct {
  size_t payload;
  uint32_t delay_ms;
  uint32_t ack_timeout_ms;
  uint16_t max_retries;
  size_t samples;
  size_t warmup;
  uint64_t seed;
  uint32_t losses_ppm[MAX_LOSS_CASES];
  size_t loss_count;
  const char *json_path;
} options_t;

typedef struct {
  uint32_t loss_ppm;
  size_t completed;
  size_t failed;
  double mean_ms;
  uint32_t p50_ms;
  uint32_t p95_ms;
  uint32_t p99_ms;
  uint32_t max_ms;
  double attempts_per_success;
  uint64_t data_drops;
  uint64_t ack_drops;
  uint32_t duplicate_rx;
  double goodput_bytes_per_s;
} case_result_t;

static void *xalloc(size_t bytes) {
  void *memory = calloc(1U, bytes);

  if (memory == NULL) {
    fputs("out of memory\n", stderr);
    exit(2);
  }
  return memory;
}

static uint64_t rng_next(uint64_t *state) {
  uint64_t x = *state;

  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  *state = x;
  return x * UINT64_C(2685821657736338717);
}

/* Uniform in [0, 1e6); a draw below loss_ppm drops the unit. */
static int rng_drop(uint64_t *state, uint32_t loss_ppm) {
  if (loss_ppm == 0U) {
    return 0;
  }
  return rng_next(state) % UINT64_C(1000000) < (uint64_t)loss_ppm;
}

static wl_sink_result_t channel_sink(void *user_data, wl_io_token_t token,
                                     const uint8_t *data, size_t length) {
  channel_t *channel = user_data;

  (void)token;
  channel->enqueued++;
  if (length > WL_FRAME_MAX_COBS_LEN ||
      rng_drop(&channel->rng, channel->loss_ppm)) {
    channel->dropped++;
    return WL_SINK_SENT;
  }
  for (size_t i = 0U; i < CHANNEL_CAPACITY; ++i) {
    if (channel->active[i] == 0U) {
      memcpy(channel->data[i], data, length);
      channel->length[i] = length;
      channel->deliver_at[i] = channel->now_ms + channel->delay_ms;
      channel->active[i] = 1U;
      return WL_SINK_SENT;
    }
  }
  /* A full channel behaves like a dropped datagram, not a protocol stall. */
  channel->dropped++;
  return WL_SINK_SENT;
}

static void channel_deliver(channel_t *channel, wl_ctx_t *peer,
                            uint32_t now_ms) {
  for (size_t i = 0U; i < CHANNEL_CAPACITY; ++i) {
    int ret;

    if (channel->active[i] == 0U || channel->deliver_at[i] > now_ms) {
      continue;
    }
    ret = wl_feed_unit(peer, channel->data[i], channel->length[i]);
    if (ret == WL_ERR_WOULD_BLOCK) {
      continue; /* Retry later, like a datagram sitting in a queue. */
    }
    channel->active[i] = 0U;
    if (ret != WL_OK) {
      channel->feed_errors++;
    }
  }
}

static void link_init(link_t *link, uint64_t session_id,
                      const options_t *options) {
  memset(link, 0, sizeof(*link));
  link->config = (wl_config_t){
      .max_payload_len = WL_FRAME_MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_NATIVE_PACKET,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = session_id,
      .max_retries = options->max_retries,
      .ack_timeout_ms = options->ack_timeout_ms,
      .max_transmission_unit = sizeof(link->tx_unit),
  };
  link->storage = (wl_storage_t){
      .tx_payload = link->tx_payload,
      .tx_payload_size = sizeof(link->tx_payload),
      .tx_unit = link->tx_unit,
      .tx_unit_size = sizeof(link->tx_unit),
      .control_unit = link->control_unit,
      .control_unit_size = sizeof(link->control_unit),
      .rx_fifo = link->rx_fifo,
      .rx_fifo_size = sizeof(link->rx_fifo),
      .rx_fallback = link->rx_fallback,
      .rx_fallback_size = sizeof(link->rx_fallback),
  };
  if (wl_init(&link->ctx, &link->config, &link->storage) != WL_OK) {
    fputs("wl_init failed\n", stderr);
    exit(2);
  }
}

/* Drain ready events without watching for a transaction. TX events are taken so
 * a stale completion cannot block the next transaction. */
static void drain(wl_ctx_t *ctx, uint32_t now_ms) {
  for (unsigned int guard = 0U; guard < 128U; ++guard) {
    wl_event_t event = {0};
    int ret = wl_poll(ctx, (wl_time_ms_t)now_ms, &event);

    if (ret != WL_OK) {
      return;
    }
    if (event.type == WL_EVT_TX_SUCCESS || event.type == WL_EVT_TX_TIMEOUT ||
        event.type == WL_EVT_TX_FAILED) {
      wl_tx_result_t ignored = {0};
      (void)wl_tx_take(ctx, event.handle, &ignored);
      continue;
    }
    wl_event_release(ctx, &event);
  }
}

/* Pump `ctx` until it has no more events or the watched transaction ends. */
static int pump_until(wl_ctx_t *ctx, uint32_t now_ms, wl_tx_handle_t watch,
                      wl_tx_result_t *out_result, int *terminal) {
  for (unsigned int guard = 0U; guard < 256U; ++guard) {
    wl_event_t event = {0};
    int ret = wl_poll(ctx, (wl_time_ms_t)now_ms, &event);

    if (ret == WL_ERR_NO_DATA) {
      return 0;
    }
    if (ret != WL_OK) {
      return ret;
    }
    if (event.type == WL_EVT_TX_SUCCESS || event.type == WL_EVT_TX_TIMEOUT ||
        event.type == WL_EVT_TX_FAILED) {
      wl_tx_result_t result = {0};

      if (wl_tx_take(ctx, event.handle, &result) != WL_OK) {
        return WL_ERR_INVALID_STATE;
      }
      if (event.handle == watch) {
        *out_result = result;
        *terminal = 1;
        return 0;
      }
      continue;
    }
    wl_event_release(ctx, &event);
  }
  return 0;
}

static int cmp_u32(const void *left, const void *right) {
  const uint32_t a = *(const uint32_t *)left;
  const uint32_t b = *(const uint32_t *)right;

  return (a > b) - (a < b);
}

/* Run one loss case and fill `result`. Returns 0 on success. */
static int run_case(const options_t *options, uint32_t loss_ppm, uint64_t seed,
                    case_result_t *result) {
  link_t client;
  link_t server;
  channel_t to_server;
  channel_t to_client;
  uint32_t *latencies = xalloc(options->samples * sizeof(uint32_t));
  uint8_t payload[WL_FRAME_MAX_PAYLOAD];
  uint32_t now = 0U;
  size_t measured = 0U;
  size_t failed = 0U;
  size_t transactions = 0U;
  uint64_t retries_total = 0U;
  const size_t total = options->samples + options->warmup;
  wl_rx_counters_t counters = {0};

  memset(&to_server, 0, sizeof(to_server));
  memset(&to_client, 0, sizeof(to_client));
  to_server.rng = seed;
  to_client.rng = seed ^ RNG_SEED;
  to_server.loss_ppm = loss_ppm;
  to_client.loss_ppm = loss_ppm;
  to_server.delay_ms = options->delay_ms;
  to_client.delay_ms = options->delay_ms;

  link_init(&client, UINT64_C(0xC11E17), options);
  link_init(&server, UINT64_C(0x5E27E2), options);
  if (wl_set_sink(&client.ctx, channel_sink, &to_server) != WL_OK ||
      wl_set_sink(&server.ctx, channel_sink, &to_client) != WL_OK) {
    free(latencies);
    return -1;
  }
  for (size_t i = 0U; i < options->payload; ++i) {
    payload[i] = (uint8_t)(0x5AU ^ (i * 31U));
  }

  while (transactions < total) {
    wl_tx_handle_t handle = 0U;
    uint32_t send_ms = now;
    wl_tx_result_t tx_result = {0};
    int terminal = 0;
    int started = 0;
    uint32_t deadline;

    for (unsigned int guard = 0U; guard < 4096U && started == 0; ++guard) {
      int ret = wl_send_reliable(&client.ctx, MESSAGE_ID, payload,
                                 options->payload, (wl_time_ms_t)now, &handle);

      if (ret == WL_OK) {
        started = 1;
        break;
      }
      if (ret != WL_ERR_WOULD_BLOCK && ret != WL_ERR_BUSY) {
        free(latencies);
        return -1;
      }
      now++;
      to_server.now_ms = to_client.now_ms = now;
      channel_deliver(&to_server, &server.ctx, now);
      channel_deliver(&to_client, &client.ctx, now);
      drain(&client.ctx, now);
      drain(&server.ctx, now);
    }
    if (started == 0) {
      free(latencies);
      return -1;
    }

    deadline = send_ms + (options->ack_timeout_ms * (uint32_t)(options->max_retries + 1U)) +
               (options->delay_ms * 8U) + 1000U;
    while (now < deadline && terminal == 0) {
      now++;
      to_server.now_ms = to_client.now_ms = now;
      channel_deliver(&to_server, &server.ctx, now);
      channel_deliver(&to_client, &client.ctx, now);
      if (pump_until(&client.ctx, now, handle, &tx_result, &terminal) != 0) {
        free(latencies);
        return -1;
      }
      drain(&server.ctx, now);
    }
    if (transactions >= options->warmup) {
      if (terminal != 0) {
        retries_total += tx_result.retries_used;
        if (tx_result.state == WL_TX_STATE_SUCCESS) {
          latencies[measured++] = now - send_ms;
        } else {
          failed++;
        }
      } else {
        failed++; /* Deadline reached without a terminal event. */
      }
    }
    transactions++;
  }

  result->loss_ppm = loss_ppm;
  result->completed = measured;
  result->failed = failed;
  result->data_drops = to_server.dropped;
  result->ack_drops = to_client.dropped;
  (void)wl_rx_get_counters(&server.ctx, &counters);
  result->duplicate_rx = counters.duplicate;

  if (measured != 0U) {
    uint64_t sum = 0U;

    qsort(latencies, measured, sizeof(uint32_t), cmp_u32);
    for (size_t i = 0U; i < measured; ++i) {
      sum += latencies[i];
    }
    result->mean_ms = (double)sum / (double)measured;
    result->p50_ms = latencies[measured / 2U];
    result->p95_ms = latencies[(measured * 95U) / 100U];
    result->p99_ms = latencies[(measured * 99U) / 100U];
    result->max_ms = latencies[measured - 1U];
    result->attempts_per_success =
        ((double)measured + (double)retries_total) / (double)measured;
    result->goodput_bytes_per_s =
        (double)measured * (double)options->payload * 1000.0 / (double)now;
  }
  free(latencies);
  return 0;
}

static uint32_t parse_percent_ppm(const char *text) {
  char *end = NULL;
  double percent = strtod(text, &end);

  if (end == text || *end != '\0' || percent < 0.0 || percent > 100.0) {
    fputs("invalid loss percent\n", stderr);
    exit(2);
  }
  return (uint32_t)(percent * 10000.0 + 0.5);
}

static void parse_loss_list(options_t *options, const char *text) {
  char *copy = xalloc(strlen(text) + 1U);
  char *cursor = copy;

  strcpy(copy, text);
  for (char *token = strtok(cursor, ","); token != NULL;
       token = strtok(NULL, ",")) {
    if (options->loss_count >= MAX_LOSS_CASES) {
      fputs("too many loss cases\n", stderr);
      exit(2);
    }
    options->losses_ppm[options->loss_count++] = parse_percent_ppm(token);
  }
  free(copy);
}

static void print_csv_header(void) {
  puts("wirelink_reliable_loss_v1,payload,delay_ms,ack_timeout_ms,max_retries,"
       "loss_ppm,samples,completed,failed,mean_ms,p50_ms,p95_ms,p99_ms,max_ms,"
       "attempts_per_success,data_drops,ack_drops,duplicate_rx,"
       "goodput_bytes_per_s");
}

static void print_csv(const options_t *options, const case_result_t *result) {
  printf("wirelink_reliable_loss_v1,%zu,%u,%u,%u,%u,%zu,%zu,%zu,%.3f,%u,%u,%u,"
         "%u,%.4f,%" PRIu64 ",%" PRIu64 ",%u,%.1f\n",
         options->payload, options->delay_ms, options->ack_timeout_ms,
         (unsigned int)options->max_retries, result->loss_ppm, options->samples,
         result->completed, result->failed, result->mean_ms, result->p50_ms,
         result->p95_ms, result->p99_ms, result->max_ms,
         result->attempts_per_success, result->data_drops, result->ack_drops,
         result->duplicate_rx, result->goodput_bytes_per_s);
}

static void print_json(const options_t *options, const case_result_t *results,
                       size_t count) {
  FILE *out = stdout;

  if (options->json_path != NULL) {
    out = fopen(options->json_path, "w");
    if (out == NULL) {
      fputs("cannot open json output\n", stderr);
      exit(2);
    }
  }
  fprintf(out,
          "{\n  \"schema\": \"wirelink-reliable-loss-v1\",\n"
          "  \"config\": {\"payload\": %zu, \"delay_ms\": %u, "
          "\"ack_timeout_ms\": %u, \"max_retries\": %u, \"samples\": %zu, "
          "\"warmup\": %zu, \"seed\": %" PRIu64 "},\n  \"cases\": [\n",
          options->payload, options->delay_ms, options->ack_timeout_ms,
          (unsigned int)options->max_retries, options->samples, options->warmup,
          options->seed);
  for (size_t i = 0U; i < count; ++i) {
    const case_result_t *r = &results[i];

    fprintf(out,
            "    {\"loss_ppm\": %u, \"completed\": %zu, \"failed\": %zu, "
            "\"mean_ms\": %.3f, \"p50_ms\": %u, \"p95_ms\": %u, "
            "\"p99_ms\": %u, \"max_ms\": %u, \"attempts_per_success\": %.4f, "
            "\"data_drops\": %" PRIu64 ", \"ack_drops\": %" PRIu64 ", "
            "\"duplicate_rx\": %u, \"goodput_bytes_per_s\": %.1f}%s\n",
            r->loss_ppm, r->completed, r->failed, r->mean_ms, r->p50_ms,
            r->p95_ms, r->p99_ms, r->max_ms, r->attempts_per_success,
            r->data_drops, r->ack_drops, r->duplicate_rx,
            r->goodput_bytes_per_s, i + 1U < count ? "," : "");
  }
  fputs("  ]\n}\n", out);
  if (options->json_path != NULL) {
    fclose(out);
  }
}

static uint32_t parse_u32(const char *text, const char *what) {
  char *end = NULL;
  unsigned long value = strtoul(text, &end, 10);

  if (end == text || *end != '\0' || value > UINT32_MAX) {
    fprintf(stderr, "invalid %s\n", what);
    exit(2);
  }
  return (uint32_t)value;
}

static void usage(const char *program) {
  fprintf(stderr,
          "usage: %s [--samples N] [--warmup N] [--payload N] [--delay-ms N]\n"
          "          [--ack-timeout-ms N] [--max-retries N] [--seed N]\n"
          "          [--loss 0,1,5,10,25,50] [--json PATH]\n",
          program);
}

int main(int argc, char **argv) {
  options_t options = {
      .payload = 64U,
      .delay_ms = 1U,
      .ack_timeout_ms = 20U,
      .max_retries = 20U,
      .samples = 2000U,
      .warmup = 32U,
      .seed = UINT64_C(0x574C524C),
      .loss_count = 0U,
      .json_path = NULL,
  };
  case_result_t results[MAX_LOSS_CASES];

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    const char *value = (i + 1 < argc) ? argv[i + 1] : NULL;

    if (strcmp(arg, "--help") == 0) {
      usage(argv[0]);
      return 0;
    }
    if (value == NULL) {
      usage(argv[0]);
      return 2;
    }
    if (strcmp(arg, "--samples") == 0) {
      options.samples = parse_u32(value, "samples");
    } else if (strcmp(arg, "--warmup") == 0) {
      options.warmup = parse_u32(value, "warmup");
    } else if (strcmp(arg, "--payload") == 0) {
      options.payload = parse_u32(value, "payload");
    } else if (strcmp(arg, "--delay-ms") == 0) {
      options.delay_ms = parse_u32(value, "delay-ms");
    } else if (strcmp(arg, "--ack-timeout-ms") == 0) {
      options.ack_timeout_ms = parse_u32(value, "ack-timeout-ms");
    } else if (strcmp(arg, "--max-retries") == 0) {
      options.max_retries = (uint16_t)parse_u32(value, "max-retries");
    } else if (strcmp(arg, "--seed") == 0) {
      options.seed = strtoull(value, NULL, 10);
    } else if (strcmp(arg, "--loss") == 0) {
      parse_loss_list(&options, value);
    } else if (strcmp(arg, "--json") == 0) {
      options.json_path = value;
    } else {
      usage(argv[0]);
      return 2;
    }
    ++i;
  }
  if (options.samples == 0U || options.payload == 0U ||
      options.payload > WL_FRAME_MAX_PAYLOAD || options.ack_timeout_ms == 0U ||
      options.loss_count == 0U) {
    usage(argv[0]);
    return 2;
  }

  for (size_t i = 0U; i < options.loss_count; ++i) {
    if (run_case(&options, options.losses_ppm[i],
                 options.seed + (uint64_t)i * RNG_SEED, &results[i]) != 0) {
      fputs("case failed\n", stderr);
      return 1;
    }
  }
  if (options.json_path == NULL) {
    print_csv_header();
    for (size_t i = 0U; i < options.loss_count; ++i) {
      print_csv(&options, &results[i]);
    }
  } else {
    print_json(&options, results, options.loss_count);
  }
  return 0;
}
