/* SPDX-License-Identifier: Apache-2.0 */
#include "../../tests/fixtures/model_transport.h"
#include "usb.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); abort(); } } while (0)
enum { ADMISSION, OWNER_STEP, IO_COMPLETION, CAPACITY_SIGNAL, PHASES };
enum { INLINE, DRAINED, SPLIT, USB_TAIL };
enum { CALLS = 32 };
typedef struct side side_t;
typedef struct {
  const uint8_t *data;
  size_t length;
  wl_packet_token_t token;
  uint32_t digest;
  unsigned state;
} flight_t;
struct side {
  usb_node_t endpoint;
  model_transport_t transport;
  flight_t flight;
  side_t *peer;
  unsigned id;
  int latched, blocked;
};
static side_t sides[2];
static unsigned phase, mode, budget, completed, effects, samples;
static uint64_t notices[PHASES], merged, steps, queries, signalled_only_steps;
static uint64_t outer_exit_wakes;
static uint64_t sent, received, busy;
static uint32_t expected;

static uint32_t digest(const uint8_t *data, size_t length) {
  uint32_t result = UINT32_C(2166136261);
  for (size_t i = 0; i < length; ++i) result = (result ^ data[i]) * UINT32_C(16777619);
  return result;
}
static void latch(side_t *side) {
  if (side->latched) ++merged;
  side->latched = 1;
}
static void notify(void *user) {
  ++notices[phase];
  latch(user);
}
static void callback_exit(side_t *side) {
  ++outer_exit_wakes;
  latch(side);
}
static int copy_packet(side_t *side, const uint8_t *data, size_t length) {
  wl_packet_claim_t claim;
  wl_err_t error = wl_packet_rx_claim(&side->peer->endpoint.node.port, length, &claim);
  if (error == WL_ERR_WOULD_BLOCK) return 0;
  CHECK(error == WL_OK);
  memcpy(claim.span.data, data, length);
  CHECK(wl_packet_rx_commit(&side->peer->endpoint.node.port, &claim, length) == WL_OK);
  ++received;
  if (mode == USB_TAIL) {
    wl_packet_activity(&side->peer->endpoint.node.port); /* Normal RX rearm publication. */
    callback_exit(side->peer); /* Outer wake after final adapter access. */
  }
  return 1;
}
static wl_packet_submit_t submit(void *user, const uint8_t *data, size_t length,
                                  wl_packet_token_t token) {
  side_t *side = user;
  if (mode == INLINE) {
    if (!copy_packet(side, data, length)) {
      side->blocked = 1;
      ++busy;
      return WL_PACKET_BUSY;
    }
    ++sent;
    return WL_PACKET_SENT;
  }
  CHECK(side->flight.state == 0U);
  side->flight = (flight_t){data, length, token, digest(data, length), 1U};
  ++sent;
  return WL_PACKET_STARTED;
}
static void service_io(side_t *side) {
  phase = CAPACITY_SIGNAL;
  if (side->blocked && wl_packet_rx_available(&side->peer->endpoint.node.port)) {
    side->blocked = 0;
    wl_packet_activity(&side->endpoint.node.port);
  }
  phase = IO_COMPLETION;
  flight_t *flight = &side->flight;
  if (flight->state == 1U) {
    CHECK(digest(flight->data, flight->length) == flight->digest);
    if (!copy_packet(side, flight->data, flight->length)) return;
    CHECK(wl_packet_tx_publish(&side->endpoint.node.port, flight->token,
          mode == SPLIT ? WL_PACKET_CONSUMED : WL_PACKET_DRAINED) == WL_OK);
    if (mode == USB_TAIL) callback_exit(side);
    flight->state = mode == SPLIT ? 2U : 0U;
  } else if (flight->state == 2U) {
    CHECK(wl_packet_tx_publish(&side->endpoint.node.port, flight->token, WL_PACKET_DRAINED) == WL_OK);
    flight->state = 0U;
  }
}
static int32_t handle(void *user, const setpoint_request_t *request,
                        setpoint_response_t *response) {
  (void)user;
  ++effects;
  *response = (setpoint_response_t){.request_id = request->request_id, .status = 0,
    .applied_value = request->value + 1U};
  return 0;
}
static void done(void *user, wl_rpc_result_t result, const setpoint_response_t *response) {
  (void)user;
  CHECK(result.outcome == WL_RPC_SUCCESS && response != NULL);
  CHECK(response->request_id == expected && response->applied_value == expected + 101U);
  ++completed;
}
static void sample(void *user, const sample_t *value) {
  side_t *side = user;
  CHECK(value->sequence == expected && value->value == 42U + (side->id ^ 1U));
  ++samples;
}
static int ready(side_t *side) {
  int has;
  uint32_t deadline;
  ++queries;
  return wl_endpoint_ready(&side->endpoint.node.core, 0U, &has, &deadline);
}
static void drive(unsigned target) {
  for (unsigned pass = 0; pass < 256U; ++pass) {
    for (unsigned i = 0; i < 2U; ++i) service_io(&sides[i]);
    for (unsigned i = 0; i < 2U; ++i) {
      side_t *side = &sides[i];
      int runnable = ready(side);
      if (runnable || side->latched) {
        if (!runnable) ++signalled_only_steps;
        /* An explicit software latch model, not an OS wait/wakeup count. */
        side->latched = 0;
        phase = OWNER_STEP;
        CHECK(wl_endpoint_step(&side->endpoint.node.core, 0U, budget) == WL_OK);
        ++steps;
      }
    }
    if (completed == target && samples == target * 2U &&
        !sides[0].flight.state && !sides[1].flight.state &&
        !sides[0].latched && !sides[1].latched &&
        !ready(&sides[0]) && !ready(&sides[1])) return;
  }
  CHECK(0);
}
static void run(unsigned selected_mode, unsigned selected_budget) {
  memset(sides, 0, sizeof(sides));
  memset(notices, 0, sizeof(notices));
  merged = steps = queries = signalled_only_steps = sent = received = busy = 0U;
  outer_exit_wakes = 0U;
  completed = effects = samples = 0U;
  phase = ADMISSION; mode = selected_mode; budget = selected_budget;
  for (unsigned i = 0; i < 2U; ++i) {
    side_t *side = &sides[i];
    side->id = i; side->peer = &sides[i ^ 1U];
    side->transport = (model_transport_t){.base = {&model_transport_ops},
        .sink = submit, .context = side, .wake = notify};
    usb_handlers_t handlers = {.on_setpoint = handle, .on_sample = sample};
    CHECK(usb_bind_instance(&side->endpoint, 100U + mode * 100U + budget * 2U + i, &side->transport.base, &handlers, side) == WL_OK);
  }
  for (expected = 0U; expected < CALLS; ++expected) {
    phase = ADMISSION;
    setpoint_request_t request = {.request_id = expected,
      .channel = 1U, .value = expected + 100U};
    wl_call_handle_t call;
    CHECK(usb_call_setpoint_ex(&sides[expected & 1U].endpoint, &request,
          0U, 1000U, done, NULL, &call) == WL_OK);
    memset(&request, 0xa5, sizeof(request));
    for (unsigned i = 0; i < 2U; ++i) {
      sample_t value = {.sequence = expected,
        .sample_tick = 7U, .value = 42U + i,
        .flags = 0U};
      CHECK(usb_send_sample(&sides[i].endpoint, &value) == WL_OK);
      memset(&value, 0xa5, sizeof(value));
    }
    drive(expected + 1U);
  }
  CHECK(completed == CALLS && effects == CALLS && samples == CALLS * 2U);
  CHECK(sent == CALLS * 4U && received == sent);
  static const char *names[] = {"inline_sent", "async_drained", "split_lease", "usb_class_model"};
  printf("{\"format\":\"wirelink-notification-census-v1\",\"timing_measurement\":false,"
    "\"case\":\"%s\",\"budget\":%u,\"calls\":%u,\"effects\":%u,\"samples\":%u,"
    "\"sent\":%" PRIu64 ",\"received\":%" PRIu64 ",\"submit_busy\":%" PRIu64 ","
    "\"notify_admission\":%" PRIu64 ",\"notify_owner_step\":%" PRIu64 ","
    "\"notify_io\":%" PRIu64 ",\"notify_capacity\":%" PRIu64 ","
    "\"outer_exit_wakes\":%" PRIu64 ","
    "\"latch_already_set\":%" PRIu64 ",\"step_calls\":%" PRIu64 ","
    "\"ready_queries\":%" PRIu64 ",\"signalled_not_ready_steps\":%" PRIu64 "}\n",
    names[mode], budget, completed, effects, samples, sent, received, busy,
    notices[ADMISSION], notices[OWNER_STEP], notices[IO_COMPLETION], notices[CAPACITY_SIGNAL], outer_exit_wakes,
    merged, steps, queries, signalled_only_steps);
  for (unsigned i = 0; i < 2U; ++i) {
    CHECK(wl_endpoint_close(&sides[i].endpoint.node.core) == WL_OK);
    CHECK(wl_endpoint_step(&sides[i].endpoint.node.core, 0U, WL_ENDPOINT_BUDGET) == WL_OK);
    CHECK(wl_endpoint_quiescent(&sides[i].endpoint.node.core));
  }
}
int main(void) {
  for (unsigned m = INLINE; m <= USB_TAIL; ++m) {
    run(m, 1U);
    run(m, WL_ENDPOINT_BUDGET);
  }
  return 0;
}
