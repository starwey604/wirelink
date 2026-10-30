/* SPDX-License-Identifier: Apache-2.0 */
/* Deterministic application/packet-time model, not a hardware CPU benchmark. */
#include "mixed_kernel.h"
#include "wirelink/packet_stream.h"
#include "workload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
#define CALLS 5U
#define DURATION 2400U

typedef struct {
  const char *name;
  unsigned busy_ms, io_ms, sample_ms;
  bool rpc, telemetry_first, wrap, drop_request, drop_response;
} scenario_t;
typedef struct simulation simulation_t;
typedef struct {
  simulation_t *simulation;
  unsigned side, sequence, began, attempts;
  bool done, retry;
} job_t;
typedef struct {
  simulation_t *simulation;
  unsigned side;
  wl_packet_port_t port;
  wl_packet_stream_t stream;
  uint8_t rx[2][128], stream_tx[80], copy[80];
  const uint8_t *borrowed;
  size_t length, fed;
  wl_packet_token_t token;
  unsigned consumed_at, drained_at, retry_at;
  bool active, retry_due, drop;
  unsigned accepted, received, last_sequence, sampled_at, received_at;
  unsigned max_age, max_arrival_age, max_gap, busy, dropped_requests, dropped_responses;
  uint64_t age_sum;
  telemetry_value_t latest;
  bool latest_pending;
  job_t jobs[CALLS];
} port_t;
struct simulation {
  scenario_t scenario;
  unsigned tick;
  int envelope;
  mixed_kernel_t endpoints[2];
  port_t ports[2];
  unsigned completed, handled, timed_out, max_rpc;
};
static simulation_t simulation;
static uint64_t next_instance = 1000;
static uint32_t now(const simulation_t *s) {
  return (s->scenario.wrap ? UINT32_MAX - 500U : 0U) + s->tick;
}
static wl_packet_submit_t transmit(void *user, const uint8_t *data, size_t length,
                                   wl_packet_token_t token) {
  port_t *p = user; simulation_t *s = p->simulation;
  CHECK(!p->active);
  if (s->scenario.busy_ms && s->tick % 250 >= 120 && s->tick % 250 < 120 + s->scenario.busy_ms) {
    ++p->busy; p->retry_at = s->tick - s->tick % 250 + 120 + s->scenario.busy_ms;
    p->retry_due = true; return WL_PACKET_BUSY;
  }
  CHECK(length <= sizeof(p->copy));
  memcpy(p->copy, data, length); p->length = length; p->fed = 0;
  p->borrowed = data; p->token = token; p->active = true;
  p->consumed_at = s->tick + 1; p->drained_at = s->tick + s->scenario.io_ms;
  /* Model loss inspects an independent copy; no source bytes are altered. */
  uint8_t raw[80]; memcpy(raw, data, length);
  size_t raw_length = length; const uint8_t *canonical = raw;
  if (s->envelope == 0) CHECK(wl_packet_cobs_crc16_decode(raw, length, &raw_length) == WL_OK);
  else if (s->envelope == 2) { CHECK(length > 2); canonical += 2; raw_length -= 2; }
  wl_kernel_frame_t frame;
  CHECK(wl_kernel_native_none_read(canonical, raw_length, &frame) == WL_OK);
  p->drop = false;
  if (frame.message_id == ECHO_REQUEST_MESSAGE_ID && s->scenario.drop_request && !p->dropped_requests) {
    ++p->dropped_requests; p->drop = true;
  }
  if (frame.message_id == ECHO_RESPONSE_MESSAGE_ID && s->scenario.drop_response && !p->dropped_responses) {
    ++p->dropped_responses; p->drop = true;
  }
  return WL_PACKET_STARTED;
}
static void service(port_t *p) {
  simulation_t *s = p->simulation;
  if (p->retry_due && s->tick >= p->retry_at) {
    p->retry_due = false; wl_packet_activity(&p->port);
  }
  if (!p->active) return;
  if (p->borrowed && s->tick >= p->consumed_at) {
    CHECK(memcmp(p->borrowed, p->copy, p->length) == 0);
    p->borrowed = NULL;
    CHECK(wl_packet_tx_publish(&p->port, p->token, WL_PACKET_CONSUMED) == WL_OK);
  }
  if (s->tick < p->drained_at) return;
  port_t *peer = &s->ports[1 - p->side];
  if (!p->drop) {
    wl_err_t error;
    if (s->envelope == 1) {
      wl_packet_claim_t claim;
      error = wl_packet_rx_claim(&peer->port, p->length, &claim);
      if (error == WL_ERR_WOULD_BLOCK) return;
      CHECK(error == WL_OK); memcpy(claim.span.data, p->copy, p->length);
      CHECK(wl_packet_rx_commit(&peer->port, &claim, p->length) == WL_OK);
    } else {
      size_t consumed = 0;
      error = s->envelope == 0 ? wl_packet_stream_cobs_crc16_feed(&peer->stream,
        p->copy + p->fed, p->length - p->fed, &consumed) :
        wl_packet_stream_length16_none_feed(&peer->stream, p->copy + p->fed, p->length - p->fed, &consumed);
      p->fed += consumed;
      CHECK(error == WL_OK || error == WL_ERR_WOULD_BLOCK);
      if (p->fed < p->length) return;
    }
  }
  CHECK(wl_packet_tx_publish(&p->port, p->token, WL_PACKET_DRAINED) == WL_OK);
  p->borrowed = NULL; p->active = false;
}
static int32_t echo(void *user, const echo_request_t *request, echo_response_value_t *response) {
  port_t *p = user; ++p->simulation->handled;
  response->has_sequence = true; response->sequence = request->sequence; return 0;
}
static void telemetry(void *user, const telemetry_t *value) {
  port_t *p = user; simulation_t *s = p->simulation;
  CHECK(value->sequence > p->last_sequence && value->sampled_at <= s->tick);
  unsigned age = s->tick - value->sampled_at;
  if (age > p->max_arrival_age) p->max_arrival_age = age;
  if (p->received && s->tick - p->received_at > p->max_gap)
    p->max_gap = s->tick - p->received_at;
  p->last_sequence = value->sequence; p->sampled_at = value->sampled_at;
  p->received_at = s->tick; ++p->received;
}
static void completed(void *user, wl_kernel_result_t result, const echo_response_t *response) {
  job_t *job = user; simulation_t *s = job->simulation;
  if (result.outcome == WL_KERNEL_TIMED_OUT) {
    CHECK(job->attempts < 3); ++s->timed_out; job->retry = true; return;
  }
  CHECK(result.outcome == WL_KERNEL_SUCCESS && response && response->sequence == job->sequence);
  CHECK(!job->done); job->done = true; ++s->completed;
  unsigned elapsed = s->tick - job->began;
  if (elapsed > s->max_rpc) s->max_rpc = elapsed;
}
static void start(job_t *job) {
  simulation_t *s = job->simulation;
  echo_request_value_t value = {.has_sequence = true, .sequence = job->sequence};
  wl_kernel_call_handle_t handle;
  CHECK(mixed_kernel_echo_start(&s->endpoints[job->side], &value, now(s), 80,
    completed, job, &handle) == WL_OK);
  memset(&value, 0, sizeof(value)); ++job->attempts; job->retry = false;
}
static void send_sample(port_t *p) {
  simulation_t *s = p->simulation;
  if (s->tick % s->scenario.sample_ms == 0) {
    p->latest = (telemetry_value_t){.has_sequence = true,
      .sequence = s->tick / s->scenario.sample_ms + 1,
      .has_sampled_at = true, .sampled_at = s->tick};
    p->latest_pending = true;
  }
  if (!p->latest_pending) return;
  wl_err_t error = mixed_kernel_send_telemetry(&s->endpoints[p->side], &p->latest);
  CHECK(error == WL_OK || error == WL_ERR_WOULD_BLOCK);
  if (error == WL_OK) { ++p->accepted; p->latest_pending = false; }
}
static void run(scenario_t scenario, int envelope, bool check) {
  simulation_t *s = &simulation; memset(s, 0, sizeof(*s));
  s->scenario = scenario; s->envelope = envelope;
  const mixed_kernel_handlers_t handlers = {.on_rpc_echo = echo, .on_message_telemetry = telemetry};
  for (unsigned side = 0; side < 2; ++side) {
    port_t *p = &s->ports[side]; p->simulation = s; p->side = side;
    wl_packet_config_t config = {.instance = ++next_instance, .rx_storage = &p->rx[0][0],
      .rx_unit_size = sizeof(p->rx[0]), .rx_storage_size = sizeof(p->rx), .submit = transmit, .user = p};
    if (envelope != 1) {
      config.submit = envelope == 0 ? wl_packet_stream_cobs_crc16_submit : wl_packet_stream_length16_none_submit;
      config.notify = wl_packet_stream_notify; config.user = &p->stream;
    }
    CHECK(wl_packet_init(&p->port, &config) == WL_OK);
    if (envelope != 1) {
      wl_packet_stream_config_t stream = {.port = &p->port, .tx_storage = {p->stream_tx, sizeof(p->stream_tx)},
        .maximum_frame_size = 64, .physical_submit = transmit, .user = p};
      CHECK((envelope == 0 ? wl_packet_stream_cobs_crc16_init(&p->stream, &stream) :
        wl_packet_stream_length16_none_init(&p->stream, &stream)) == WL_OK);
    }
    CHECK(mixed_kernel_init(&s->endpoints[side], &p->port, &handlers, NULL, p) == WL_OK);
  }
#ifdef MIXED_TRAFFIC_FIRMWARE
  mixed_fw_case_begin();
#endif
  for (s->tick = 0; s->tick < DURATION; ++s->tick) {
#ifdef MIXED_TRAFFIC_FIRMWARE
    mixed_fw_tick_begin();
#endif
    for (unsigned side = 0; side < 2; ++side) service(&s->ports[side]);
    for (unsigned side = 0; side < 2; ++side) {
      port_t *p = &s->ports[side];
      if (scenario.rpc && s->tick >= 100 && (s->tick - 100) % 400 == 0 && (s->tick - 100) / 400 < CALLS) {
        unsigned index = (s->tick - 100) / 400;
        p->jobs[index] = (job_t){.simulation = s, .side = side, .sequence = index + 1, .began = s->tick};
        start(&p->jobs[index]);
      }
      for (unsigned index = 0; index < CALLS; ++index) if (p->jobs[index].retry) start(&p->jobs[index]);
    }
    if (scenario.telemetry_first) for (unsigned side = 0; side < 2; ++side) send_sample(&s->ports[side]);
    for (unsigned side = 0; side < 2; ++side) CHECK(mixed_kernel_step(&s->endpoints[side], now(s)) == WL_OK);
    if (!scenario.telemetry_first) for (unsigned side = 0; side < 2; ++side) send_sample(&s->ports[side]);
    for (unsigned side = 0; side < 2; ++side) {
      port_t *p = &s->ports[side];
      if (p->received) {
        unsigned age = s->tick - p->sampled_at, gap = s->tick - p->received_at;
        if (age > p->max_age) p->max_age = age;
        if (gap > p->max_gap) p->max_gap = gap;
        p->age_sum += age;
      }
    }
#ifdef MIXED_TRAFFIC_FIRMWARE
    mixed_fw_tick_end();
#endif
  }
#ifdef MIXED_TRAFFIC_FIRMWARE
  mixed_fw_case_end(scenario.name, envelope);
#endif
  CHECK(s->completed == (scenario.rpc ? CALLS * 2 : 0));
  CHECK(s->handled == s->completed + s->ports[0].dropped_responses + s->ports[1].dropped_responses);
  CHECK(s->timed_out == s->ports[0].dropped_requests + s->ports[1].dropped_requests +
    s->ports[0].dropped_responses + s->ports[1].dropped_responses);
  for (unsigned side = 0; side < 2; ++side) {
    port_t *p = &s->ports[side], *sender = &s->ports[1 - side];
    CHECK(p->received && p->received <= sender->accepted);
    CHECK((p->busy > 0) == (scenario.busy_ms != 0));
    CHECK(p->dropped_requests == (unsigned)scenario.drop_request && p->dropped_responses == (unsigned)scenario.drop_response);
    if (check) {
      unsigned bound = scenario.busy_ms + scenario.sample_ms + 16 * scenario.io_ms + 8;
      CHECK(p->max_age <= bound && p->max_gap <= bound);
    }
    printf("{\"format\":2,\"clock\":\"simulated_ms\",\"duration_ms\":2400,\"scenario\":\"%s\",\"envelope\":%d,\"receiver\":%u,"
      "\"accepted\":%u,\"received\":%u,\"max_age_ms\":%u,\"max_arrival_age_ms\":%u,\"max_gap_ms\":%u,\"mean_age_ms\":%.6f,"
      "\"rpc_completed\":%u,\"rpc_handled\":%u,\"rpc_timed_out\":%u,\"max_rpc_ms\":%u,\"dropped_requests\":%u,\"dropped_responses\":%u,\"busy\":%u}\n",
      scenario.name, envelope, side, sender->accepted, p->received, p->max_age, p->max_arrival_age,
      p->max_gap, (double)p->age_sum / DURATION, s->completed, s->handled, s->timed_out,
      s->max_rpc, p->dropped_requests, p->dropped_responses, p->busy);
  }
  for (unsigned side = 0; side < 2; ++side) {
    port_t *p = &s->ports[side]; CHECK(wl_kernel_close(&s->endpoints[side].core) == WL_OK);
    bool cancelled = p->active;
    if (p->active) {
      /* Model driver abandons its actual held source/copy before TX_ERROR. */
      p->borrowed = NULL; p->active = false;
      CHECK(wl_packet_tx_publish(&p->port, p->token, WL_PACKET_TX_ERROR) == WL_OK);
    }
    if (envelope != 1) CHECK(wl_packet_stream_abort_rx(&p->stream) == WL_OK);
    wl_err_t error = mixed_kernel_step(&s->endpoints[side], now(s));
    CHECK(error == WL_OK || (cancelled && error == WL_ERR_IO));
    CHECK(wl_kernel_quiescent(&s->endpoints[side].core));
  }
}
int mixed_traffic_run(bool check) {
  const scenario_t scenarios[] = {
    {.name = "telemetry_only", .io_ms = 1, .sample_ms = 5},
    {.name = "rpc_clean", .io_ms = 1, .sample_ms = 5, .rpc = true},
    {.name = "drop_request", .io_ms = 1, .sample_ms = 5, .rpc = true, .drop_request = true},
    {.name = "drop_response", .io_ms = 1, .sample_ms = 5, .rpc = true, .drop_response = true},
    {.name = "backpressure", .busy_ms = 20, .io_ms = 1, .sample_ms = 5, .rpc = true},
    {.name = "loss_backpressure", .busy_ms = 20, .io_ms = 1, .sample_ms = 5, .rpc = true, .drop_request = true, .drop_response = true},
    {.name = "saturated_wrap", .busy_ms = 20, .io_ms = 3, .sample_ms = 1, .rpc = true,
      .telemetry_first = true, .wrap = true, .drop_request = true, .drop_response = true},
  };
  for (int envelope = 0; envelope < 3; ++envelope)
    for (unsigned i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) run(scenarios[i], envelope, check);
  return 0;
}
