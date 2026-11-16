/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef WL_PACKET_ZTEST
#include <zephyr/ztest.h>
#define CHECK(x) zassert_true((x), "%s:%d: %s", __FILE__, __LINE__, #x)
#else
#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)
#endif

typedef struct fixture fixture_t;
typedef struct {
  fixture_t *fixture;
  unsigned count;
  uint32_t value;
  wl_kernel_result_t result;
  wl_kernel_call_handle_t handle;
} completion_t;

struct fixture {
  wl_kernel_t kernel;
  wl_packet_port_t port;
  uint8_t rx[2][128];
  uint8_t tx[WL_KERNEL_TX_COUNT][64];
  uint8_t frames[32][64];
  size_t lengths[32];
  unsigned submitted;
  unsigned attempts;
  unsigned handled;
  unsigned messages;
  unsigned errors;
  wl_err_t last_error;
  wl_packet_submit_t sink_result;
  const uint8_t *leased;
  size_t leased_length;
  wl_packet_token_t token;
  completion_t completions[8];
  int callback_action;
  unsigned message_start;
};

static wl_err_t send_msg(wl_kernel_t *kernel, uint16_t id, uint32_t value) {
  if (value == 13U) return WL_ERR_CORRUPT_PAYLOAD;
  uint8_t *frame;
  size_t capacity = 0U;
  wl_err_t error = wl_kernel_msg_claim(kernel, &frame, &capacity);
  if (error != WL_OK) return error;
  if (capacity < WL_KERNEL_NATIVE_HEADER + sizeof(uint32_t))
    return WL_ERR_BUF_TOO_SMALL;
  memcpy(frame + WL_KERNEL_NATIVE_HEADER, &value, sizeof(uint32_t));
  error = wl_wire_frame_seal(frame, capacity, WL_WIRE_MSG, id, sizeof(uint32_t));
  return error != WL_OK ? error
                        : wl_kernel_msg_commit(kernel, WL_KERNEL_NATIVE_HEADER + sizeof(uint32_t));
}

static void erased_callback(void) {}
static wl_err_t start(fixture_t *, unsigned, uint32_t, uint32_t, uint32_t);
static wl_err_t start_auto(fixture_t *, unsigned, uint32_t, wl_kernel_call_handle_t *);

static void complete(wl_kernel_t *kernel, const wl_kernel_call_t *call,
                      wl_kernel_result_t result, const uint8_t *body, size_t length) {
  completion_t *c = call->user;
  fixture_t *f = c->fixture;
  ++c->count;
  c->result = result;
  if (result.outcome == WL_KERNEL_SUCCESS) {
    CHECK(length == sizeof(c->value));
    memcpy(&c->value, body, length);
  }
  if (f->callback_action) {
    f->callback_action = 0;
    CHECK(wl_kernel_step(kernel, 100U, 16U) == WL_ERR_REENTRANT);
    CHECK(wl_kernel_close(kernel) == WL_ERR_REENTRANT);
    CHECK(wl_kernel_cancel(kernel, f->completions[1].handle) == WL_OK);
    /* New call is already due relative to this step's now. Its generation
     * must exclude it from the current terminal scan. */
    CHECK(start(f, 4U, 3U, 0U, 1U) == WL_OK);
    CHECK(f->completions[1].count == 0U && f->completions[4].count == 0U);
  }
}

static wl_err_t start(fixture_t *f, unsigned index, uint32_t value,
                       uint32_t now, uint32_t timeout) {
  uint8_t slot;
  uint8_t *payload;
  size_t capacity = 0U;
  wl_err_t error = wl_kernel_call_claim(&f->kernel, &slot, &payload, &capacity);
  if (error != WL_OK) {
    f->completions[index].handle = (wl_kernel_call_handle_t){0};
    return error;
  }
  if (value == 13U) {
    f->completions[index].handle = (wl_kernel_call_handle_t){0};
    return WL_ERR_CORRUPT_PAYLOAD;
  }
  if (capacity < sizeof(uint32_t)) return WL_ERR_BUF_TOO_SMALL;
  memcpy(payload, &value, sizeof(uint32_t));
  return wl_kernel_call_commit(&f->kernel, slot, 100U, 101U, sizeof(uint32_t),
      now, timeout, 1U, complete, erased_callback, &f->completions[index],
      &f->completions[index].handle);
}

static wl_err_t start_auto(fixture_t *f, unsigned index, uint32_t timeout,
                            wl_kernel_call_handle_t *out) {
  uint8_t slot;
  uint8_t *payload;
  wl_err_t error = wl_kernel_call_claim(&f->kernel, &slot, &payload, NULL);
  if (error != WL_OK) return error;
  memset(payload, 0, sizeof(uint32_t));
  return wl_kernel_call_commit(&f->kernel, slot, 100U, 101U, sizeof(uint32_t),
      UINT32_MAX, timeout, 0U, complete, erased_callback, &f->completions[index], out);
}

static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t length,
                               wl_packet_token_t token) {
  fixture_t *f = user;
  ++f->attempts;
  if (f->sink_result == WL_PACKET_BUSY || f->sink_result == WL_PACKET_FAILED)
    return f->sink_result;
  CHECK(f->submitted < 32U && length <= 64U);
  memcpy(f->frames[f->submitted], data, length);
  f->lengths[f->submitted++] = length;
  f->leased = data;
  f->leased_length = length;
  f->token = token;
  return f->sink_result;
}

static void error(void *user, uint16_t id, wl_err_t code) {
  fixture_t *f = user;
  (void)id;
  ++f->errors;
  f->last_error = code;
}

/* Unit fixture codec/handler. Generated business codecs and routes have their
 * own end-to-end fixture; this deliberately tests only the shared kernel. */
static wl_err_t route(wl_kernel_t *kernel, const wl_kernel_frame_t *frame) {
  fixture_t *f = (fixture_t *)kernel;
  if (frame->message_id == 101U || frame->message_id == 106U)
    return wl_kernel_response(kernel, frame);
  if (frame->message_id == 102U) {
    if (frame->kind != WL_WIRE_MSG) return WL_ERR_CORRUPT_PAYLOAD;
    ++f->messages;
    if (f->message_start) {
      unsigned action = f->message_start;
      f->message_start = 0U;
      CHECK((action == 3U
          ? start_auto(f, 0U, 1U, &f->completions[0].handle)
          : start(f, 0U, 1U, 0U, 1U)) == WL_OK);
      if (action == 2U)
        CHECK(wl_kernel_cancel(kernel, f->completions[0].handle) == WL_OK);
    }
    return WL_OK;
  }
  if (frame->message_id != 100U) return WL_ERR_NOT_SUPPORTED;
  if (frame->kind != WL_WIRE_RPC_REQ) return WL_ERR_CORRUPT_PAYLOAD;
  wl_kernel_rpc_metadata_t metadata;
  wl_err_t status = wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
  if (status != WL_OK) return status;
  uint8_t reply;
  uint8_t *payload;
  size_t capacity = 0U;
  status = wl_kernel_reply_claim(kernel, 101U, &metadata, &reply, &payload, &capacity);
  if (status != WL_OK) return status;
  if (metadata.length != sizeof(uint32_t)) {
    wl_kernel_reply_abort(kernel, reply);
    return WL_ERR_CORRUPT_PAYLOAD;
  }
  uint32_t value;
  memcpy(&value, metadata.body, sizeof(value));
  ++f->handled;
  ++value;
  if (value == 10U) return wl_kernel_reply_error(kernel, reply, -77);
  if (value == 13U) {
    wl_kernel_reply_abort(kernel, reply);
    return WL_ERR_CORRUPT_PAYLOAD;
  }
  if (capacity < sizeof(uint32_t)) {
    wl_kernel_reply_abort(kernel, reply);
    return WL_ERR_BUF_TOO_SMALL;
  }
  memcpy(payload, &value, sizeof(uint32_t));
  return wl_kernel_reply_commit(kernel, reply, sizeof(uint32_t));
}

static void init(fixture_t *f, uint32_t instance) {
  wl_packet_config_t port = {
      .instance = instance, .rx_storage = &f->rx[0][0], .rx_unit_size = sizeof(f->rx[0]),
      .rx_storage_size = sizeof(f->rx), .submit = sink, .user = f
  };
  CHECK(wl_packet_init(&f->port, &port) == WL_OK);
  wl_kernel_config_t config = {.port = &f->port,
      .storage = wl_kernel_storage_uniform(&f->tx[0][0], sizeof(f->tx[0])),
      .route = route, .on_error = error, .user = f};
  CHECK(wl_kernel_init(&f->kernel, &config) == WL_OK);
  for (unsigned i = 0U; i < 8U; ++i) f->completions[i].fixture = f;
}

static void inject(fixture_t *f, uint16_t id, uint32_t session, uint32_t operation,
                     int32_t status, uint32_t value) {
  wl_packet_claim_t claim;
  CHECK(wl_packet_rx_claim(&f->port, 64U, &claim) == WL_OK);
  size_t body_len = 0U;
  uint8_t kind = id == 102U ? WL_WIRE_MSG
                 : (id == 100U ? WL_WIRE_RPC_REQ
                               : (status == 0 ? WL_WIRE_RPC_RESP : WL_WIRE_RPC_ERR));
  if (kind == WL_WIRE_MSG) {
    memcpy(claim.span.data + 4U, &value, 4U);
    body_len = 4U;
  } else {
    CHECK(wl_wire_rpc_write(claim.span.data + 4U, 60U, session, operation, status, &body_len) == WL_OK);
    if (status == 0) { memcpy(claim.span.data + 4U + body_len, &value, 4U); body_len += 4U; }
  }
  CHECK(wl_wire_frame_seal(claim.span.data, 64U, kind, id, body_len) == WL_OK);
  CHECK(wl_packet_rx_commit(&f->port, &claim, 4U + body_len) == WL_OK);
}

static int ready(fixture_t *f, uint32_t now) {
  int has_deadline;
  uint32_t deadline;
  return wl_kernel_ready(&f->kernel, now, &has_deadline, &deadline);
}

static void run(fixture_t *f, uint32_t now) {
  for (unsigned i = 0U; i < 32U; ++i) {
    CHECK(wl_kernel_step(&f->kernel, now, WL_KERNEL_BUDGET) == WL_OK);
    if (!ready(f, now)) return;
  }
  CHECK(0); /* BUSY must eventually sleep rather than spin. */
}

static void test_calls(void) {
  fixture_t f = {0};
  init(&f, 111U);
  for (unsigned i = 0U; i < 4U; ++i) CHECK(start(&f, i, i, 0U, 100U) == WL_OK);
  uint32_t operation = f.kernel.operation;
  CHECK(start(&f, 4U, 5U, 0U, 100U) == WL_ERR_WOULD_BLOCK);
  CHECK(f.kernel.operation == operation && f.submitted == 0U);
  run(&f, 0U);
  CHECK(f.submitted == 4U);
  for (unsigned i = 0U; i < 4U; ++i) {
    wl_kernel_frame_t frame;
    wl_kernel_rpc_metadata_t metadata;
    CHECK(wl_wire_frame_read(f.frames[i], f.lengths[i], &frame) == WL_OK);
    CHECK(frame.kind == WL_WIRE_RPC_REQ);
    CHECK(wl_wire_rpc_read(frame.kind, frame.payload, frame.length, &metadata) == WL_OK);
    uint32_t value;
    memcpy(&value, metadata.body, sizeof(value));
    CHECK(value == i); /* Request was snapshotted before stack input expired. */
  }
  inject(&f, 106U, 111U, f.completions[0].handle.operation, 0, 99U);
  run(&f, 1U);
  inject(&f, 101U, 110U, f.completions[0].handle.operation, 0, 99U);
  run(&f, 1U);
  CHECK(f.completions[0].count == 0U);
  const unsigned order[] = {2U, 0U, 3U, 1U};
  for (unsigned i = 0U; i < 4U; ++i) {
    unsigned slot = order[i];
    inject(&f, 101U, 111U, f.completions[slot].handle.operation, slot == 3U ? -9 : 0, slot + 20U);
    run(&f, 2U);
    CHECK(f.completions[slot].count == 1U);
    CHECK(f.completions[slot].result.outcome == (slot == 3U ? WL_KERNEL_REJECTED : WL_KERNEL_SUCCESS));
    CHECK(slot == 3U ? f.completions[slot].result.status == -9 : f.completions[slot].value == slot + 20U);
  }
  inject(&f, 101U, 111U, f.completions[0].handle.operation, 0, 99U);
  run(&f, 3U);
  CHECK(f.completions[0].count == 1U);
  CHECK(wl_kernel_cancel(&f.kernel, f.completions[0].handle) == WL_ERR_NOT_FOUND);
}

static void test_physical_lease_and_callbacks(void) {
  fixture_t f = {.sink_result = WL_PACKET_STARTED};
  init(&f, 222U);
  CHECK(start(&f, 0U, 0U, 0U, 10U) == WL_OK);
  run(&f, 0U);
  CHECK(f.submitted == 1U);
  const uint8_t *leased = f.leased;
  size_t length = f.leased_length;
  uint8_t saved[64];
  memcpy(saved, leased, length);
  CHECK(wl_kernel_cancel(&f.kernel, f.completions[0].handle) == WL_OK);
  CHECK(f.completions[0].count == 0U);
  run(&f, 1U);
  CHECK(f.completions[0].count == 1U && f.completions[0].result.outcome == WL_KERNEL_CANCELLED);
  for (unsigned i = 1U; i < 4U; ++i) CHECK(start(&f, i, i, 1U, 10U) == WL_OK);
  CHECK(start(&f, 4U, 4U, 1U, 10U) == WL_ERR_WOULD_BLOCK);
  CHECK(memcmp(saved, leased, length) == 0);
  CHECK(wl_packet_tx_publish(&f.port, f.token, WL_PACKET_CONSUMED) == WL_OK);
  run(&f, 2U);
  CHECK(start(&f, 4U, 4U, 2U, 10U) == WL_ERR_WOULD_BLOCK);
  CHECK(memcmp(saved, leased, length) == 0);
  CHECK(wl_packet_tx_publish(&f.port, f.token, WL_PACKET_DRAINED) == WL_OK);
  f.sink_result = WL_PACKET_SENT;
  run(&f, 2U);
  CHECK(start(&f, 4U, 4U, 2U, 10U) == WL_OK);
  inject(&f, 101U, 222U, f.completions[0].handle.operation, 0, 88U);
  run(&f, 3U);
  CHECK(f.completions[0].count == 1U);

  fixture_t g = {0};
  init(&g, 333U);
  CHECK(start(&g, 0U, 1U, 0U, 10U) == WL_OK);
  CHECK(start(&g, 1U, 2U, 0U, 100U) == WL_OK);
  run(&g, 0U);
  g.callback_action = 1;
  CHECK(wl_kernel_step(&g.kernel, 10U, 16U) == WL_OK);
  CHECK(g.completions[0].count == 1U && g.completions[0].result.outcome == WL_KERNEL_TIMED_OUT);
  CHECK(g.completions[1].count == 1U && g.completions[1].result.outcome == WL_KERNEL_CANCELLED);
  CHECK(g.completions[4].count == 0U);
  run(&g, 10U);
  CHECK(g.completions[4].count == 1U && g.completions[4].result.outcome == WL_KERNEL_TIMED_OUT);
}

static void test_reply_backpressure(void) {
  fixture_t f = {.sink_result = WL_PACKET_BUSY};
  init(&f, 444U);
  for (unsigned i = 0U; i < WL_KERNEL_REPLIES; ++i) {
    inject(&f, 100U, 999U, i + 1U, 0, i);
    run(&f, 0U);
  }
  CHECK(f.handled == WL_KERNEL_REPLIES && f.submitted == 0U);
  inject(&f, 100U, 999U, WL_KERNEL_REPLIES + 1U, 0, WL_KERNEL_REPLIES);
  run(&f, 0U);
  CHECK(f.handled == WL_KERNEL_REPLIES && f.kernel.rx_held);
  unsigned attempts = f.attempts;
  CHECK(!ready(&f, 0U));
  CHECK(wl_kernel_step(&f.kernel, 0U, 16U) == WL_OK);
  CHECK(f.attempts == attempts && f.handled == WL_KERNEL_REPLIES);
  f.sink_result = WL_PACKET_SENT;
  wl_packet_activity(&f.port);
  run(&f, 0U);
  CHECK(f.handled == WL_KERNEL_REPLIES + 1U && f.submitted == WL_KERNEL_REPLIES + 1U && !f.kernel.rx_held);
  /* No replay: independently admitted duplicate runs handler again. */
  inject(&f, 100U, 999U, WL_KERNEL_REPLIES + 1U, 0, WL_KERNEL_REPLIES);
  run(&f, 0U);
  CHECK(f.handled == WL_KERNEL_REPLIES + 2U);
  inject(&f, 100U, 999U, WL_KERNEL_REPLIES + 2U, 0, 12U); /* Reply encode fails after handler. */
  CHECK(wl_kernel_step(&f.kernel, 0U, 16U) == WL_ERR_CORRUPT_PAYLOAD);
  CHECK(f.errors == 1U && f.handled == WL_KERNEL_REPLIES + 3U);
  inject(&f, 100U, 999U, WL_KERNEL_REPLIES + 3U, 0, 9U);
  run(&f, 0U);
  wl_kernel_frame_t frame;
  wl_kernel_rpc_metadata_t metadata;
  CHECK(wl_wire_frame_read(f.frames[f.submitted - 1U], f.lengths[f.submitted - 1U], &frame) == WL_OK);
  CHECK(frame.kind == WL_WIRE_RPC_ERR);
  CHECK(wl_wire_rpc_read(frame.kind, frame.payload, frame.length, &metadata) == WL_OK);
  CHECK(metadata.status == -77 && metadata.length == 0U);
}

static void test_errors_close_and_wrap(void) {
  fixture_t f = {.sink_result = WL_PACKET_FAILED};
  init(&f, 555U);
  CHECK(start(&f, 0U, 13U, 0U, 10U) == WL_ERR_CORRUPT_PAYLOAD);
  CHECK(f.kernel.operation == 0U && f.completions[0].count == 0U);
  CHECK(start(&f, 0U, 1U, UINT32_MAX - 4U, 10U) == WL_OK);
  CHECK(wl_kernel_step(&f.kernel, UINT32_MAX - 4U, 16U) == WL_ERR_IO);
  CHECK(f.completions[0].count == 1U && f.completions[0].result.outcome == WL_KERNEL_LOCAL_ERROR);
  f.sink_result = WL_PACKET_BUSY;
  CHECK(start(&f, 1U, 1U, UINT32_MAX - 4U, 10U) == WL_OK);
  run(&f, UINT32_MAX - 4U);
  int has;
  uint32_t deadline;
  CHECK(!wl_kernel_ready(&f.kernel, 0U, &has, &deadline) && has && deadline == 5U);
  run(&f, 5U);
  CHECK(f.completions[1].count == 1U && f.completions[1].result.outcome == WL_KERNEL_TIMED_OUT);
  f.sink_result = WL_PACKET_STARTED;
  wl_packet_activity(&f.port);
  CHECK(start(&f, 2U, 2U, 5U, 10U) == WL_OK);
  run(&f, 5U);
  wl_packet_token_t old_token = f.token;
  CHECK(start(&f, 3U, 3U, 5U, 10U) == WL_OK);
  CHECK(wl_kernel_close(&f.kernel) == WL_OK);
  CHECK(!wl_kernel_quiescent(&f.kernel));
  run(&f, 5U);
  CHECK(f.completions[2].count == 1U && f.completions[3].count == 1U);
  CHECK(f.completions[2].result.outcome == WL_KERNEL_CLOSED);
  CHECK(!wl_kernel_quiescent(&f.kernel));
  CHECK(wl_packet_tx_publish(&f.port, old_token, WL_PACKET_DRAINED) == WL_OK);
  run(&f, 5U);
  CHECK(wl_kernel_quiescent(&f.kernel));
  wl_kernel_call_handle_t old_call = f.completions[3].handle;
  init(&f, 556U);
  CHECK(wl_kernel_cancel(&f.kernel, old_call) == WL_ERR_NOT_FOUND);
  CHECK(wl_packet_tx_publish(&f.port, old_token, WL_PACKET_DRAINED) == WL_ERR_NOT_FOUND);
  f.kernel.operation = UINT32_MAX;
  CHECK(start(&f, 4U, 4U, 0U, 10U) == WL_ERR_ID_EXHAUSTED);
  wl_packet_claim_t claim;
  CHECK(wl_packet_rx_claim(&f.port, 64U, &claim) == WL_OK);
  memset(claim.span.data, 0, 4U);
  CHECK(wl_packet_rx_commit(&f.port, &claim, 4U) == WL_OK);
  CHECK(wl_kernel_step(&f.kernel, 0U, 16U) == WL_ERR_BAD_FRAME);
  CHECK(send_msg(&f.kernel, 102U, 7U) == WL_OK);
  f.sink_result = WL_PACKET_SENT;
  run(&f, 0U);
}

static void test_tx_admission_order(void) {
  fixture_t f = {.sink_result = WL_PACKET_BUSY};
  init(&f, 901U);
  CHECK(send_msg(&f.kernel, 102U, 7U) == WL_OK);
  CHECK(start(&f, 0U, 1U, 0U, 100U) == WL_OK);
  CHECK(start(&f, 1U, 2U, 0U, 100U) == WL_OK);
  run(&f, 0U);
  CHECK(f.submitted == 0U);
  /* Cancel/reuse a queued owner index without moving the new request ahead. */
  CHECK(wl_kernel_cancel(&f.kernel, f.completions[0].handle) == WL_OK);
  run(&f, 0U);
  CHECK(start(&f, 2U, 3U, 0U, 100U) == WL_OK);
  f.sink_result = WL_PACKET_SENT;
  wl_packet_activity(&f.port);
  run(&f, 0U);
  CHECK(f.submitted == 3U);
  wl_kernel_frame_t frame;
  CHECK(wl_kernel_native_none_read(f.frames[0], f.lengths[0], &frame) == WL_OK);
  CHECK(frame.message_id == 102U);
  wl_kernel_rpc_metadata_t metadata;
  for (unsigned i = 1U; i < 3U; ++i) {
    CHECK(wl_wire_frame_read(f.frames[i], f.lengths[i], &frame) == WL_OK);
    CHECK(frame.message_id == 100U && frame.kind == WL_WIRE_RPC_REQ);
    CHECK(wl_wire_rpc_read(frame.kind, frame.payload, frame.length, &metadata) == WL_OK);
    CHECK(metadata.operation == i + 1U);
  }
}

static void test_budget_deadline_and_response_boundary(void) {
  fixture_t f = {.sink_result = WL_PACKET_BUSY};
  init(&f, 903U);
  for (unsigned i = 0U; i < 4U; ++i) CHECK(start(&f, i, i, 0U, 10U) == WL_OK);
  run(&f, 0U);
  CHECK(f.submitted == 0U && !ready(&f, 0U));
  inject(&f, 102U, 700U, 1U, 0, 99U);
  inject(&f, 102U, 700U, 2U, 0, 99U);
  /* Even a one-event pass makes deadlines progress ahead of a full RX queue.
   * Requests have never reached the sink: timeout includes queue residence. */
  for (unsigned i = 0U; i < 4U; ++i) {
    CHECK(ready(&f, 10U));
    CHECK(wl_kernel_step(&f.kernel, 10U, 1U) == WL_OK);
    CHECK(f.completions[i].count == 1U && f.completions[i].result.outcome == WL_KERNEL_TIMED_OUT);
    CHECK(f.messages == 0U);
  }
  CHECK(wl_kernel_step(&f.kernel, 10U, 1U) == WL_OK && ready(&f, 10U));
  CHECK(wl_kernel_step(&f.kernel, 10U, 1U) == WL_OK && !ready(&f, 10U));
  CHECK(f.messages == 2U && f.submitted == 0U);
  CHECK(wl_kernel_close(&f.kernel) == WL_OK);
  CHECK(start(&f, 4U, 1U, 10U, 10U) == WL_ERR_INVALID_STATE);
  CHECK(send_msg(&f.kernel, 102U, 7U) == WL_ERR_INVALID_STATE);
  CHECK(wl_kernel_step(&f.kernel, 10U, 16U) == WL_OK && wl_kernel_quiescent(&f.kernel));

  fixture_t g = {0};
  init(&g, 904U);
  CHECK(start(&g, 0U, 1U, 0U, 10U) == WL_OK);run(&g, 0U);
  inject(&g, 101U, 904U, g.completions[0].handle.operation, 0, 99U);
  /* The owner's exact deadline wins over a response still queued at that
   * snapshot. Terminal is delivered once, before processing the same RX. */
  run(&g, 10U);
  CHECK(g.completions[0].count == 1U && g.completions[0].result.outcome == WL_KERNEL_TIMED_OUT);
  CHECK(start(&g, 1U, 2U, 10U, 100U) == WL_OK);run(&g, 10U);
  inject(&g, 101U, 904U, g.completions[0].handle.operation, 0, 99U);run(&g, 11U);
  CHECK(g.completions[0].count == 1U && g.completions[1].count == 0U);
  inject(&g, 101U, 904U, g.completions[1].handle.operation, 0, 42U);run(&g, 11U);
  CHECK(g.completions[1].count == 1U && g.completions[1].value == 42U);
  fixture_t h = {0};init(&h, 905U);
  for (unsigned i = 0U; i < 4U; ++i) h.kernel.calls[i].generation = UINT32_MAX;
  CHECK(start(&h, 0U, 1U, 0U, 10U) == WL_ERR_ID_EXHAUSTED && h.kernel.operation == 0U);
}

static void test_simultaneous_deadlines(void) {
  for (unsigned budget = 1U; budget <= WL_KERNEL_BUDGET; ++budget) {
    for (unsigned calls = 2U; calls <= WL_KERNEL_CALLS; calls += 2U) {
      for (unsigned wrap = 0U; wrap < 2U; ++wrap) {
        uint32_t begin = wrap ? UINT32_MAX - 4U : 0U;
        uint32_t deadline = begin + 10U;
        for (unsigned boundary = 0U; boundary < 3U; ++boundary) {
          for (unsigned cancel = 0U; cancel < 2U; ++cancel) {
            fixture_t f = {0};
            init(&f, 906U);
            for (unsigned i = 0U; i < calls; ++i)
              CHECK(start(&f, i, i, begin, 10U) == WL_OK);
            run(&f, begin);
            CHECK(f.submitted == calls);
            if (cancel) CHECK(wl_kernel_cancel(&f.kernel, f.completions[0].handle) == WL_OK);
            inject(&f, 101U, 906U, f.completions[calls - 1U].handle.operation, 0, 42U);
            uint32_t now = deadline - 1U + boundary;
            for (unsigned pass = 0U; pass < 16U && ready(&f, now); ++pass)
              CHECK(wl_kernel_step(&f.kernel, now, budget) == WL_OK);
            CHECK(!ready(&f, now));
            CHECK(!wl_packet_rx_ready(&f.port));
            for (unsigned i = 0U; i < calls; ++i) {
              completion_t *c = &f.completions[i];
              if (cancel && i == 0U)
                CHECK(c->count == 1U && c->result.outcome == WL_KERNEL_CANCELLED);
              else if (boundary == 0U && i == calls - 1U)
                CHECK(c->count == 1U && c->result.outcome == WL_KERNEL_SUCCESS && c->value == 42U);
              else if (boundary != 0U)
                CHECK(c->count == 1U && c->result.outcome == WL_KERNEL_TIMED_OUT);
              else CHECK(c->count == 0U);
            }
            run(&f, deadline + 1U);
            for (unsigned i = 0U; i < calls; ++i) CHECK(f.completions[i].count == 1U);
            inject(&f, 101U, 906U, f.completions[calls - 1U].handle.operation, 0, 99U);
            run(&f, deadline + 2U);
            CHECK(f.completions[calls - 1U].count == 1U);
          }
        }
      }
    }
    fixture_t f = {0};
    init(&f, 907U);
    CHECK(start(&f, 0U, 1U, 0U, 10U) == WL_OK);
    CHECK(start(&f, 1U, 2U, 0U, 100U) == WL_OK);
    run(&f, 0U);
    f.callback_action = 1;
    CHECK(wl_kernel_step(&f.kernel, 10U, budget) == WL_OK);
    CHECK(f.completions[0].count == 1U && f.completions[4].count == 0U);
    run(&f, 10U);
    CHECK(f.completions[1].count == 1U && f.completions[1].result.outcome == WL_KERNEL_CANCELLED);
    CHECK(f.completions[4].count == 1U && f.completions[4].result.outcome == WL_KERNEL_TIMED_OUT);
  }
}

static void test_small_budget_io_fairness(void) {
  for (unsigned budget = 1U; budget <= WL_KERNEL_BUDGET; ++budget) {
    for (unsigned asynchronous = 0U; asynchronous < 2U; ++asynchronous) {
      fixture_t f = {0};
      init(&f, 908U);
      CHECK(start(&f, 0U, 1U, 0U, 10U) == WL_OK);
      run(&f, 0U);
      CHECK(f.submitted == 1U);
      inject(&f, 101U, 908U, f.completions[0].handle.operation, 0, 42U);
      f.sink_result = asynchronous ? WL_PACKET_STARTED : WL_PACKET_SENT;
      uint32_t sequence = 0U;
      unsigned completion_stage = 0U;
      for (uint32_t now = 1U; now <= 8U; ++now) {
        uint32_t sample = 7U;
        wl_err_t error = send_msg(&f.kernel, 102U, sample);
        CHECK(error == WL_OK || error == WL_ERR_WOULD_BLOCK);
        /* Keep RX busy as well: granting RX a turn must not starve TX. */
        if (wl_packet_rx_available(&f.port)) inject(&f, 102U, 700U, now, 0, sample);
        CHECK(wl_kernel_step(&f.kernel, now, budget) == WL_OK);
        if (asynchronous && wl_packet_tx_active(&f.port)) {
          if (sequence != f.token.sequence) {
            sequence = f.token.sequence;
            completion_stage = 0U;
          }
          if (completion_stage < 2U) {
            CHECK(wl_packet_tx_publish(&f.port, f.token,
                completion_stage == 0U ? WL_PACKET_CONSUMED : WL_PACKET_DRAINED) == WL_OK);
            ++completion_stage;
          }
        }
        /* Even budget=1 services the response before its deadline, despite
         * continuous bidirectional telemetry and split physical completion. */
        if (now >= 4U)
          CHECK(f.completions[0].count == 1U && f.completions[0].result.outcome == WL_KERNEL_SUCCESS);
      }
      CHECK(f.completions[0].value == 42U);
      CHECK(f.messages != 0U && f.submitted >= 3U);
      run(&f, 11U);
      CHECK(f.completions[0].count == 1U && f.completions[0].result.outcome == WL_KERNEL_SUCCESS);
    }
  }
}

static void test_empty_entry_generation_snapshot(void) {
  for (unsigned budget = 1U; budget <= WL_KERNEL_BUDGET; ++budget) {
    for (unsigned action = 1U; action <= 2U; ++action) {
      fixture_t f = {0};
      init(&f, 909U);
      CHECK(!ready(&f, 10U));
      f.message_start = action;
      /* RX creates an already-due (or cancelled) call after an empty entry
       * snapshot. Its new generation must wait for the next owner step. */
      inject(&f, 102U, 700U, 1U, 0, 7U);
      CHECK(wl_kernel_step(&f.kernel, 10U, budget) == WL_OK);
      CHECK(f.completions[0].count == 0U);
      CHECK(ready(&f, 10U));
      run(&f, 10U);
      CHECK(f.completions[0].count == 1U);
      CHECK(f.completions[0].result.outcome ==
          (action == 1U ? WL_KERNEL_TIMED_OUT : WL_KERNEL_CANCELLED));
      CHECK(wl_kernel_close(&f.kernel) == WL_OK);
      run(&f, 10U);
      CHECK(wl_kernel_quiescent(&f.kernel));
      init(&f, 910U);
      CHECK(!ready(&f, 10U));
    }
  }
}

static void test_optional_storage(void) {
  for (unsigned mask = 0; mask < 8U; ++mask) {
    fixture_t f = {0};
    init(&f, 911U);
    wl_kernel_config_t config = {.port = &f.port, .storage = {
        .requests = mask & 1U ? &f.tx[0][0] : NULL,
        .request_stride = mask & 1U ? 64U : 0U,
        .replies = mask & 2U ? &f.tx[4][0] : NULL,
        .reply_stride = mask & 2U ? 64U : 0U,
        .message = mask & 4U ? f.tx[7] : NULL,
        .message_capacity = mask & 4U ? 64U : 0U,
    }};
    CHECK(wl_kernel_init(&f.kernel, &config) == WL_OK);
    uint8_t slot, *data;
    CHECK(wl_kernel_call_claim(&f.kernel, &slot, &data, NULL) ==
        (mask & 1U ? WL_OK : WL_ERR_NOT_SUPPORTED));
    CHECK(wl_kernel_msg_claim(&f.kernel, &data, NULL) ==
        (mask & 4U ? WL_OK : WL_ERR_NOT_SUPPORTED));
    f.kernel.in_step = 1U;
    const wl_kernel_rpc_metadata_t metadata = {.session = 12U, .operation = 1U};
    CHECK(wl_kernel_reply_claim(&f.kernel, 101U, &metadata, &slot, &data, NULL) ==
        (mask & 2U ? WL_OK : WL_ERR_NOT_SUPPORTED));
    if (mask & 2U) wl_kernel_reply_abort(&f.kernel, slot);
    f.kernel.in_step = 0U;
    inject(&f, 102U, 0U, 0U, 0, 1U);
    CHECK(wl_kernel_step(&f.kernel, 0U, WL_KERNEL_BUDGET) == WL_ERR_NOT_SUPPORTED);
    CHECK(!wl_packet_rx_ready(&f.port));
    CHECK(wl_kernel_close(&f.kernel) == WL_OK);
    CHECK(wl_kernel_quiescent(&f.kernel));
    /* Reject every mismatched pointer/capacity pair, not just small buffers. */
    config.storage.requests = NULL;
    config.storage.request_stride = 12U;
    CHECK(wl_kernel_init(&f.kernel, &config) == WL_ERR_INVALID_ARG);
    config.storage.request_stride = 0U;
    config.storage.replies = f.tx[4];
    config.storage.reply_stride = 0U;
    CHECK(wl_kernel_init(&f.kernel, &config) == WL_ERR_INVALID_ARG);
    config.storage.replies = NULL;
    config.storage.message = f.tx[7];
    config.storage.message_capacity = 0U;
    CHECK(wl_kernel_init(&f.kernel, &config) == WL_ERR_INVALID_ARG);
  }
}

static void test_deferred_time(void) {
  fixture_t f = {0};
  init(&f, 912U);
  CHECK(wl_kernel_step(&f.kernel, 1000U, WL_KERNEL_BUDGET) == WL_OK);
  CHECK(start_auto(&f, 0U, 500U, NULL) == WL_OK);
  int has_deadline = -1;
  uint32_t deadline = 0U;
  CHECK(wl_kernel_ready(&f.kernel, 100000U, &has_deadline, &deadline));
  CHECK(!has_deadline);
  CHECK(wl_kernel_step(&f.kernel, 100000U, WL_KERNEL_BUDGET) == WL_OK);
  CHECK(f.completions[0].count == 0U && f.submitted == 1U);
  (void)wl_kernel_ready(&f.kernel, 100499U, &has_deadline, &deadline);
  CHECK(has_deadline && deadline == 100500U);
  CHECK(wl_kernel_step(&f.kernel, 100499U, WL_KERNEL_BUDGET) == WL_OK);
  CHECK(f.completions[0].count == 0U);
  CHECK(wl_kernel_step(&f.kernel, 100500U, WL_KERNEL_BUDGET) == WL_OK);
  CHECK(f.completions[0].count == 1U);
  CHECK(f.completions[0].result.outcome == WL_KERNEL_TIMED_OUT);
  CHECK(start_auto(&f, 1U, 10U, &f.completions[1].handle) == WL_OK);
  CHECK(wl_kernel_cancel(&f.kernel, f.completions[1].handle) == WL_OK);
  CHECK(wl_kernel_step(&f.kernel, 100500U, WL_KERNEL_BUDGET) == WL_OK);
  CHECK(f.completions[1].count == 1U &&
      f.completions[1].result.outcome == WL_KERNEL_CANCELLED);
  CHECK(start_auto(&f, 2U, 10U, NULL) == WL_OK);
  CHECK(wl_kernel_close(&f.kernel) == WL_OK);
  CHECK(wl_kernel_step(&f.kernel, 100500U, WL_KERNEL_BUDGET) == WL_OK);
  CHECK(f.completions[2].count == 1U &&
      f.completions[2].result.outcome == WL_KERNEL_CLOSED);
  CHECK(wl_kernel_quiescent(&f.kernel));
  for (unsigned budget = 1U; budget <= WL_KERNEL_BUDGET; ++budget) {
    fixture_t nested = {0};
    init(&nested, 913U);
    nested.message_start = 3U;
    inject(&nested, 102U, 700U, 1U, 0, 7U);
    CHECK(wl_kernel_step(&nested.kernel, 100000U, budget) == WL_OK);
    CHECK(nested.completions[0].count == 0U);
    CHECK(wl_kernel_ready(&nested.kernel, UINT32_MAX, &has_deadline, &deadline));
    CHECK(!has_deadline);
    /* A callback-created call arms on the next step, including across wrap. */
    CHECK(wl_kernel_step(&nested.kernel, UINT32_MAX, WL_KERNEL_BUDGET) == WL_OK);
    CHECK(nested.completions[0].count == 0U);
    CHECK(wl_kernel_step(&nested.kernel, 0U, WL_KERNEL_BUDGET) == WL_OK);
    CHECK(nested.completions[0].count == 1U &&
        nested.completions[0].result.outcome == WL_KERNEL_TIMED_OUT);
  }
}

#ifdef WL_PACKET_ZTEST
ZTEST(static_kernel, optional_storage) { test_optional_storage(); }
ZTEST(static_kernel, deferred_time) { test_deferred_time(); }
ZTEST(static_kernel, empty_entry_generation_snapshot) { test_empty_entry_generation_snapshot(); }
ZTEST(static_kernel, calls) { test_calls(); }
ZTEST(static_kernel, physical_lease_and_callbacks) { test_physical_lease_and_callbacks(); }
ZTEST(static_kernel, reply_backpressure) { test_reply_backpressure(); }
ZTEST(static_kernel, errors_close_and_wrap) { test_errors_close_and_wrap(); }
ZTEST(static_kernel, tx_admission_order) { test_tx_admission_order(); }
ZTEST(static_kernel, budget_deadline_and_response_boundary) { test_budget_deadline_and_response_boundary(); }
ZTEST(static_kernel, simultaneous_deadlines) { test_simultaneous_deadlines(); }
ZTEST(static_kernel, small_budget_io_fairness) { test_small_budget_io_fairness(); }
ZTEST_SUITE(static_kernel, NULL, NULL, NULL, NULL, NULL);
#else
int main(void) {
  test_optional_storage();
  test_deferred_time();
  test_empty_entry_generation_snapshot();
  test_small_budget_io_fairness();
  test_simultaneous_deadlines();
  test_budget_deadline_and_response_boundary();
  test_tx_admission_order();
  test_calls();
  test_physical_lease_and_callbacks();
  test_reply_backpressure();
  test_errors_close_and_wrap();
  return 0;
}
#endif
