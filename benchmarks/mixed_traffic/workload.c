/* SPDX-License-Identifier: Apache-2.0 */
/* Deterministic protocol-time experiment, NOT a CPU or physical UART benchmark. */
#include "mixed_endpoint.h"
#include "wirelink/cobs.h"
#include "wirelink/wirelink.h"
#include "workload.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); abort(); \
} } while (0)
#define CALLS 5U
#define DURATION 2400U

typedef struct {
  const char *name;
  unsigned dropped_acks, busy_ms, io_ms, telemetry_ms;
  bool rpc, telemetry_first, wrap;
  unsigned ack_drop_limit; /* Zero: apply loss to each reliable sequence. */
} scenario_t;

typedef struct simulation simulation_t;
typedef struct {
  simulation_t *simulation;
  unsigned started, sequence;
} call_t;

typedef struct {
  simulation_t *simulation;
  unsigned index;
  wl_endpoint_t *owner;
  wl_ctx_t *link, *peer;
  const uint8_t *borrowed;
  size_t length;
  wl_io_token_t token;
  unsigned complete_at;
  uint8_t copy[MIXED_ENDPOINT_UNIT_CAPACITY];
  uint32_t ack_sequence;
  bool have_ack, drop;
  unsigned ack_copies, ack_drops, busy;
  unsigned accepted, received, last_sequence, sampled_at, received_at;
  unsigned max_age, max_arrival_age, max_gap;
  uint64_t age_sum;
  telemetry_t latest;
  unsigned sent_sequence;
  call_t calls[CALLS];
} port_t;

struct simulation {
  scenario_t scenario;
  wl_envelope_type_t envelope;
  unsigned tick;
  uint64_t identity;
  mixed_endpoint_t endpoints[2];
  port_t ports[2];
  unsigned completed, handled, max_rpc_ms;
};

static wl_time_ms_t now(void *context) {
  const simulation_t *s = context;
  return (s->scenario.wrap ? UINT32_MAX - 500U : 0U) + s->tick;
}

static wl_err_t next_session(void *context, uint64_t *session) {
  simulation_t *s = context;
  *session = ++s->identity;
  return WL_OK;
}

static wl_sink_result_t transmit(void *context, wl_io_token_t token,
                                  const uint8_t *data, size_t length) {
  port_t *p = context;
  simulation_t *s = p->simulation;
  CHECK(p->borrowed == NULL); /* Core must serialize the physical resource. */
  if (s->scenario.busy_ms != 0U && s->tick % 250U >= 120U &&
      s->tick % 250U < 120U + s->scenario.busy_ms) {
    ++p->busy;
    return WL_SINK_BUSY;
  }
  CHECK(length <= sizeof(p->copy));
  memcpy(p->copy, data, length);
  uint8_t decoded[MIXED_ENDPOINT_UNIT_CAPACITY];
  const uint8_t *raw = data;
  size_t raw_length = length;
  if (s->envelope == WL_ENVELOPE_COBS_STREAM) {
    CHECK(length > 1U && data[length - 1U] == 0U);
    CHECK(wl_cobs_decode(data, length - 1U, decoded, sizeof(decoded), &raw_length) == WL_OK);
    raw = decoded;
  } else if (s->envelope == WL_ENVELOPE_BUS_LENGTH16) {
    CHECK(length >= 2U);
    raw += 2U;
    raw_length -= 2U;
  }
  wl_frame_view_t frame;
  CHECK(wl_frame_decode(raw, raw_length, WL_INTEGRITY_CRC32C, &frame) == WL_OK);
  p->drop = false;
  if (frame.type == WL_PACKET_ACK) {
    if (!p->have_ack || p->ack_sequence != frame.sequence) {
      p->have_ack = true;
      p->ack_sequence = frame.sequence;
      p->ack_copies = 0U;
    }
    p->drop = p->ack_copies++ < s->scenario.dropped_acks &&
        (s->scenario.ack_drop_limit == 0U || p->ack_drops < s->scenario.ack_drop_limit);
    if (p->drop) ++p->ack_drops;
  }
  p->borrowed = data;
  p->length = length;
  p->token = token;
  p->complete_at = s->tick + s->scenario.io_ms;
  return WL_SINK_STARTED;
}

static int service(void *context) {
  port_t *p = context;
  if (p->borrowed == NULL || p->simulation->tick < p->complete_at)
    return WL_ERR_NO_DATA;
  CHECK(memcmp(p->borrowed, p->copy, p->length) == 0);
  if (!p->drop) {
    if (p->simulation->envelope == WL_ENVELOPE_COBS_STREAM) {
      size_t accepted;
      CHECK(wl_feed_bytes(p->peer, p->copy, p->length, &accepted) == WL_OK);
      CHECK(accepted == p->length);
    } else CHECK(wl_feed_unit(p->peer, p->copy, p->length) == WL_OK);
  }
  p->borrowed = NULL;
  CHECK(wl_tx_complete(p->link, p->token, WL_OK) == WL_OK);
  return WL_OK;
}

static void quiesce(void *context) {
  port_t *p = context;
  /* The simulation cancels outstanding physical I/O at close. */
  CHECK(wl_set_sink(p->link, NULL, NULL) == WL_OK);
  if (p->borrowed != NULL) {
    p->borrowed = NULL;
    CHECK(wl_tx_complete(p->link, p->token, WL_OK) == WL_OK);
  }
}

static int32_t echo(void *context, const echo_request_value_t *request,
                    echo_response_value_t *response) {
  simulation_t *s = context;
  ++s->handled;
  response->has_sequence = true;
  response->sequence = request->sequence;
  return 0;
}

static void completed(void *context, const wl_rpc_completion_t *result,
                       const echo_response_value_t *response) {
  call_t *call = context;
  simulation_t *s = call->simulation;
  CHECK(result->status == WL_RPC_SUCCESS);
  CHECK(response != NULL && response->sequence == call->sequence);
  unsigned elapsed = s->tick - call->started;
  if (elapsed > s->max_rpc_ms) s->max_rpc_ms = elapsed;
  ++s->completed;
}

static void telemetry(simulation_t *s, unsigned index) {
  port_t *p = &s->ports[index];
  if (s->tick % s->scenario.telemetry_ms == 0U) {
    p->latest.has_sequence = p->latest.has_sampled_at = true;
    ++p->latest.sequence;
    p->latest.sampled_at = s->tick;
  }
  if (p->sent_sequence == p->latest.sequence) return;
  mixed_send_result_t result = mixed_endpoint_send_telemetry(&s->endpoints[index], &p->latest);
  if (result.domain == MIXED_SEND_OK) {
    ++p->accepted;
    p->sent_sequence = p->latest.sequence;
  } else CHECK(result.core_result == WL_ERR_BUSY || result.core_result == WL_ERR_WOULD_BLOCK);
}

static void observe(simulation_t *s, unsigned index) {
  port_t *p = &s->ports[index];
  telemetry_t latest = {0};
  int result = mixed_endpoint_read_telemetry(&s->endpoints[index], &latest);
  CHECK(result == WL_OK || result == WL_ERR_NO_DATA);
  if (result == WL_OK) {
    CHECK(latest.has_sequence && latest.has_sampled_at);
    CHECK(latest.sampled_at <= s->tick && latest.sequence > p->last_sequence);
    const unsigned gap = s->tick - p->received_at;
    const unsigned arrival_age = s->tick - latest.sampled_at;
    if (p->received != 0U && gap > p->max_gap) p->max_gap = gap;
    if (arrival_age > p->max_arrival_age) p->max_arrival_age = arrival_age;
    ++p->received;
    p->last_sequence = latest.sequence;
    p->sampled_at = latest.sampled_at;
    p->received_at = s->tick;
  }
  /* read consumes the mailbox revision, not the application's last value.
   * Its age continues increasing on EVERY tick with no new reception. */
  if (p->received == 0U) return;
  const unsigned age = s->tick - p->sampled_at;
  if (age > p->max_age) p->max_age = age;
  s->ports[index].age_sum += age;
}

static void run(scenario_t scenario, wl_envelope_type_t envelope, bool check) {
  static simulation_t s;
  memset(&s, 0, sizeof(s));
  s.scenario = scenario;
  s.envelope = envelope;
  const wl_environment_t environment = {{now, &s}, {next_session, &s}};
  for (unsigned i = 0U; i < 2U; ++i) {
    mixed_endpoint_config_t config;
    CHECK(mixed_endpoint_config_defaults(&config, environment) == WL_OK);
    config.link.envelope = envelope;
    config.link.ack_timeout_ms = 100U;
    config.link.max_retries = 4U;
    config.on_echo = echo;
    config.user_data = &s;
    CHECK(mixed_endpoint_init_config(&s.endpoints[i], &config) == WL_OK);
    s.ports[i].simulation = &s;
    s.ports[i].index = i;
    s.ports[i].owner = mixed_endpoint_handle(&s.endpoints[i]);
    s.ports[i].link = wl_endpoint_link(s.ports[i].owner);
  }
  for (unsigned i = 0U; i < 2U; ++i) {
    port_t *p = &s.ports[i];
    p->peer = s.ports[1U - i].link;
    wl_pump_hooks_t hooks = {.adapter_user_data = p, .service = service, .quiesce = quiesce};
    CHECK(wl_endpoint_attach(p->owner, &hooks) == WL_OK);
    CHECK(wl_set_sink(p->link, transmit, p) == WL_OK);
  }
#ifdef MIXED_TRAFFIC_H7
  mixed_h7_case_begin();
#endif
  for (s.tick = 0U; s.tick < DURATION; ++s.tick) {
#ifdef MIXED_TRAFFIC_H7
    mixed_h7_tick_begin();
#endif
    if (scenario.rpc && s.tick >= 100U && (s.tick - 100U) % 400U == 0U &&
        (s.tick - 100U) / 400U < CALLS) {
      const unsigned ordinal = (s.tick - 100U) / 400U;
      for (unsigned i = 0U; i < 2U; ++i) {
        call_t *call = &s.ports[i].calls[ordinal];
        *call = (call_t){&s, s.tick, i * CALLS + ordinal + 1U};
        echo_request_value_t request = {.has_sequence = true, .sequence = call->sequence};
        CHECK(mixed_endpoint_echo_async(&s.endpoints[i], &request, 1500U,
                                         completed, call, NULL) == WL_OK);
      }
    }
    if (scenario.telemetry_first) for (unsigned i = 0U; i < 2U; ++i) telemetry(&s, i);
    for (unsigned i = 0U; i < 2U; ++i) CHECK(mixed_endpoint_step(&s.endpoints[i]) == WL_OK);
    if (!scenario.telemetry_first) for (unsigned i = 0U; i < 2U; ++i) telemetry(&s, i);
    for (unsigned i = 0U; i < 2U; ++i) observe(&s, i);
#ifdef MIXED_TRAFFIC_H7
    mixed_h7_tick_end();
#endif
  }
#ifdef MIXED_TRAFFIC_H7
  mixed_h7_case_end(scenario.name, envelope);
#endif
  CHECK(s.completed == (scenario.rpc ? CALLS * 2U : 0U));
  CHECK(s.handled == s.completed); /* Retries must not repeat business work. */
  for (unsigned i = 0U; i < 2U; ++i) {
    port_t *p = &s.ports[i];
    CHECK(p->received > 0U);
    if (scenario.rpc && scenario.dropped_acks != 0U) CHECK(p->ack_drops > 0U);
    if (scenario.busy_ms != 0U) CHECK(p->busy > 0U);
    printf("{\"format\":1,\"clock\":\"simulated_ms\",\"duration_ms\":%u,"
           "\"scenario\":\"%s\",\"envelope\":%d,\"receiver\":%u,"
           "\"accepted\":%u,\"received\":%u,\"max_age_ms\":%u,"
           "\"max_arrival_age_ms\":%u,\"max_gap_ms\":%u,\"mean_age_ms\":%.3f,"
           "\"rpc_completed\":%u,\"max_rpc_ms\":%u,\"ack_drops\":%u,\"busy\":%u}\n",
           DURATION, scenario.name, envelope, i, s.ports[1U - i].accepted, p->received,
           p->max_age, p->max_arrival_age, p->max_gap, (double)p->age_sum / DURATION,
           s.completed, s.max_rpc_ms, p->ack_drops, p->busy);
    if (check) {
      /* Finite physical backpressure/serialization remain observable; ACK
       * timeout (100 ms) must not determine telemetry freshness. */
      const unsigned bound = scenario.busy_ms + scenario.telemetry_ms + 12U * scenario.io_ms;
      CHECK(p->max_age <= bound && p->max_gap <= bound);
    }
  }
  for (unsigned i = 0U; i < 2U; ++i) CHECK(mixed_endpoint_close(&s.endpoints[i]) == WL_OK);
}

int mixed_traffic_run(bool check) {
  const scenario_t scenarios[] = {
    {"telemetry_only", 0, 0, 1, 5, false, false, false, 0},
    {"rpc_clean", 0, 0, 1, 5, true, false, false, 0},
    {"single_ack", 1, 0, 1, 5, true, false, false, 1},
    {"drop_ack", 2, 0, 1, 5, true, false, false, 0},
    {"backpressure", 0, 20, 1, 5, true, false, false, 0},
    {"drop_ack_backpressure", 2, 20, 1, 5, true, false, false, 0},
    {"saturated_wrap", 2, 20, 3, 1, true, true, true, 0},
  };
  for (int envelope = WL_ENVELOPE_COBS_STREAM; envelope <= WL_ENVELOPE_BUS_LENGTH16; ++envelope)
    for (size_t i = 0U; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
      run(scenarios[i], envelope, check);
  return 0;
}
