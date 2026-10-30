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
};

static wl_err_t encode(const void *value, uint8_t *data, size_t capacity, size_t *length) {
  if (capacity < sizeof(uint32_t)) return WL_ERR_BUF_TOO_SMALL;
  if (*(const uint32_t *)value == 13U) return WL_ERR_CORRUPT_PAYLOAD;
  memcpy(data, value, sizeof(uint32_t));
  *length = sizeof(uint32_t);
  return WL_OK;
}

static void erased_callback(void) {}
static wl_err_t start(fixture_t *, unsigned, uint32_t, uint32_t, uint32_t);

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
  return wl_kernel_call_start(&f->kernel, 100U, 101U, &value, encode, now, timeout,
      complete, erased_callback, &f->completions[index], &f->completions[index].handle);
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
    return wl_kernel_response(kernel, frame->message_id, frame->payload, frame->length);
  if (frame->message_id == 102U) { ++f->messages; return WL_OK; }
  if (frame->message_id != 100U) return WL_ERR_NOT_SUPPORTED;
  wl_kernel_rpc_metadata_t metadata;
  wl_err_t status = wl_kernel_rpc_read(frame->payload, frame->length, 1, &metadata);
  if (status != WL_OK) return status;
  unsigned reply;
  status = wl_kernel_reply_reserve(kernel, &reply);
  if (status != WL_OK) return status;
  if (metadata.length != sizeof(uint32_t)) {
    wl_kernel_reply_abort(kernel, reply);
    return WL_ERR_CORRUPT_PAYLOAD;
  }
  uint32_t value;
  memcpy(&value, metadata.body, sizeof(value));
  ++f->handled;
  ++value;
  return wl_kernel_reply_finish(kernel, reply, 101U, &metadata,
      value == 10U ? -77 : 0, &value, encode);
}

static void init(fixture_t *f, uint64_t instance) {
  wl_packet_config_t port = {
      .instance = instance, .rx_storage = &f->rx[0][0], .rx_unit_size = sizeof(f->rx[0]),
      .rx_storage_size = sizeof(f->rx), .submit = sink, .user = f
  };
  CHECK(wl_packet_init(&f->port, &port) == WL_OK);
  wl_kernel_config_t config = {.port = &f->port, .route = route, .on_error = error, .user = f};
  for (unsigned i = 0U; i < WL_KERNEL_TX_COUNT; ++i)
    config.tx_storage[i] = (wl_span_t){f->tx[i], sizeof(f->tx[i])};
  CHECK(wl_kernel_init(&f->kernel, &config) == WL_OK);
  for (unsigned i = 0U; i < 8U; ++i) f->completions[i].fixture = f;
}

static void inject(fixture_t *f, uint16_t id, uint64_t session, uint32_t operation,
                     int32_t status, uint32_t value) {
  wl_packet_claim_t claim;
  CHECK(wl_packet_rx_claim(&f->port, 64U, &claim) == WL_OK);
  size_t header;
  CHECK(wl_kernel_rpc_write(claim.span.data + 4U, 60U, session, operation, status, &header) == WL_OK);
  if (status == 0) { memcpy(claim.span.data + 4U + header, &value, 4U); header += 4U; }
  CHECK(wl_kernel_native_none_seal(claim.span.data, 64U, id, header) == WL_OK);
  CHECK(wl_packet_rx_commit(&f->port, &claim, 4U + header) == WL_OK);
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
    CHECK(wl_kernel_native_none_read(f.frames[i], f.lengths[i], &frame) == WL_OK);
    CHECK(wl_kernel_rpc_read(frame.payload, frame.length, 1, &metadata) == WL_OK);
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
  for (unsigned i = 0U; i < 4U; ++i) {
    inject(&f, 100U, 999U, i + 1U, 0, i);
    run(&f, 0U);
  }
  CHECK(f.handled == 4U && f.submitted == 0U);
  inject(&f, 100U, 999U, 5U, 0, 4U);
  run(&f, 0U);
  CHECK(f.handled == 4U && f.kernel.rx_held);
  unsigned attempts = f.attempts;
  CHECK(!ready(&f, 0U));
  CHECK(wl_kernel_step(&f.kernel, 0U, 16U) == WL_OK);
  CHECK(f.attempts == attempts && f.handled == 4U);
  f.sink_result = WL_PACKET_SENT;
  wl_packet_activity(&f.port);
  run(&f, 0U);
  CHECK(f.handled == 5U && f.submitted == 5U && !f.kernel.rx_held);
  /* No replay: independently admitted duplicate runs handler again. */
  inject(&f, 100U, 999U, 5U, 0, 4U);
  run(&f, 0U);
  CHECK(f.handled == 6U);
  inject(&f, 100U, 999U, 6U, 0, 12U); /* Reply encode fails after handler. */
  CHECK(wl_kernel_step(&f.kernel, 0U, 16U) == WL_ERR_CORRUPT_PAYLOAD);
  CHECK(f.errors == 1U && f.handled == 7U);
  inject(&f, 100U, 999U, 7U, 0, 9U);
  run(&f, 0U);
  wl_kernel_frame_t frame;
  wl_kernel_rpc_metadata_t metadata;
  CHECK(wl_kernel_native_none_read(f.frames[f.submitted - 1U], f.lengths[f.submitted - 1U], &frame) == WL_OK);
  CHECK(wl_kernel_rpc_read(frame.payload, frame.length, 0, &metadata) == WL_OK);
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
  uint32_t sample = 7U;
  CHECK(wl_kernel_send(&f.kernel, 102U, &sample, encode) == WL_OK);
  f.sink_result = WL_PACKET_SENT;
  run(&f, 0U);
}

#ifdef WL_PACKET_ZTEST
ZTEST(static_kernel, calls) { test_calls(); }
ZTEST(static_kernel, physical_lease_and_callbacks) { test_physical_lease_and_callbacks(); }
ZTEST(static_kernel, reply_backpressure) { test_reply_backpressure(); }
ZTEST(static_kernel, errors_close_and_wrap) { test_errors_close_and_wrap(); }
ZTEST_SUITE(static_kernel, NULL, NULL, NULL, NULL, NULL);
#else
int main(void) {
  test_calls();
  test_physical_lease_and_callbacks();
  test_reply_backpressure();
  test_errors_close_and_wrap();
  return 0;
}
#endif
