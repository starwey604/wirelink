/* SPDX-License-Identifier: Apache-2.0 */
#include "session_kernel.h"
#include "wirelink/environment.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("SESSION line=%d: %s FAIL\n", __LINE__, #x); return 1; } } while (0)
typedef struct {
  session_kernel_t endpoint;
  wl_packet_port_t port;
  uint8_t rx[2][2053];
  wl_packet_port_t *peer;
  uint8_t captured[96];
  size_t length;
  uint64_t previous;
} owner_t;
static owner_t client, server;
static uint64_t identity = UINT64_C(0x123456789abcdef0);
static unsigned source_reads, executed, notifications;
static uint32_t now;
static int source_mode;
static response_value_t owned;
static wl_kernel_result_t result;
static wl_err_t next_identity(void *context, uint64_t *out) {
  (void)context; ++source_reads;
  if (source_mode == 1) return WL_ERR_IO;
  *out = source_mode == 2 ? 0 : source_mode == 3 ? identity : ++identity;
  return WL_OK;
}
static wl_err_t inject(wl_packet_port_t *peer, const uint8_t *data, size_t length) {
  wl_packet_claim_t claim;
  wl_err_t error = wl_packet_rx_claim(peer, length, &claim);
  if (error != WL_OK) return error;
  memcpy(claim.span.data, data, length);
  return wl_packet_rx_commit(peer, &claim, length);
}
static wl_packet_submit_t send(void *user, const uint8_t *data, size_t length,
                               wl_packet_token_t token) {
  owner_t *owner = user; (void)token;
  if (length > sizeof(owner->captured)) return WL_PACKET_FAILED;
  wl_err_t error = inject(owner->peer, data, length);
  if (error == WL_ERR_WOULD_BLOCK) return WL_PACKET_BUSY;
  if (error != WL_OK) return WL_PACKET_FAILED;
  memcpy(owner->captured, data, length); owner->length = length;
  /* A synchronous RAM driver: SENT follows the actual peer copy. */
  return WL_PACKET_SENT;
}
static int32_t execute(void *user, const request_t *request, response_value_t *response) {
  (void)user; ++executed;
  if (request->input < 0) return 7;
  response->has_output = true; response->output = request->input + 1; return 0;
}
static void completed(void *user, wl_kernel_result_t value, const response_t *view) {
  (void)user; result = value; ++notifications;
  if (value.outcome == WL_KERNEL_SUCCESS) {
    /* No borrowed response survives this callback. */
    if (!view || response_value_from_view(view, &owned) != WL_CODEC_OK)
      result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
}
static wl_err_t init(owner_t *owner, wl_session_source_t source, bool service) {
  uint64_t fresh = 0;
  wl_err_t error = wl_session_next(source, owner->previous, &fresh);
  if (error != WL_OK) return error;
  wl_packet_config_t config = {.instance = fresh, .rx_storage = &owner->rx[0][0],
    .rx_unit_size = sizeof(owner->rx[0]), .rx_storage_size = sizeof(owner->rx),
    .submit = send, .user = owner};
  error = wl_packet_init(&owner->port, &config);
  if (error != WL_OK) return error;
  session_kernel_handlers_t handlers = {.on_rpc_execute = service ? execute : NULL};
  error = session_kernel_init(&owner->endpoint, &owner->port, &handlers, NULL, NULL);
  if (error == WL_OK) owner->previous = fresh;
  return error;
}
static int close_owner(owner_t *owner) {
  CHECK(wl_kernel_close(&owner->endpoint.core) == WL_OK);
  /* RAM submit never retains a physical source after returning. */
  wl_packet_stop(&owner->port);
  CHECK(session_kernel_step(&owner->endpoint, now) == WL_OK);
  CHECK(wl_kernel_quiescent(&owner->endpoint.core)); return 0;
}
static int drain(void) {
  for (unsigned i = 0; i < 8; ++i) {
    /* A peer credit change wakes a previously busy synchronous driver. */
    if (wl_packet_rx_available(&server.port)) wl_packet_activity(&client.port);
    if (wl_packet_rx_available(&client.port)) wl_packet_activity(&server.port);
    CHECK(session_kernel_step(&client.endpoint, now) == WL_OK);
    CHECK(session_kernel_step(&server.endpoint, now) == WL_OK); ++now;
  }
  return 0;
}
static wl_err_t call(int value, wl_kernel_call_handle_t *handle) {
  request_value_t request = {.has_input = true, .input = value};
  return session_kernel_execute_start(&client.endpoint, &request, now, 100,
    completed, NULL, handle);
}
int session_run(wl_session_source_t native) {
  wl_session_source_t source = {next_identity, NULL};
  wl_kernel_call_handle_t old, current;
  uint64_t sentinel = 42;
  source_mode = 1;
  CHECK(wl_session_next(source, 0, &sentinel) == WL_ERR_IO && sentinel == 42);
  CHECK(init(&client, source, false) == WL_ERR_IO);
  source_mode = 2;
  CHECK(wl_session_next(source, 0, &sentinel) == WL_ERR_INVALID_STATE && sentinel == 42);
  CHECK(init(&client, source, false) == WL_ERR_INVALID_STATE);
  source_mode = 3;
  CHECK(wl_session_next(source, identity, &sentinel) == WL_ERR_INVALID_STATE && sentinel == 42);
  source_mode = 0;
  client.peer = &server.port; server.peer = &client.port;
  CHECK(init(&client, source, false) == WL_OK && init(&server, source, true) == WL_OK);
  CHECK(client.previous != server.previous);
  unsigned reads = source_reads;
  CHECK(call(40, &old) == WL_OK && old.operation == 1 && drain() == 0);
  CHECK(notifications == 1 && executed == 1 && result.outcome == WL_KERNEL_SUCCESS && owned.output == 41);
  uint8_t stale_request[96], stale_success[96], stale_reject[96];
  size_t request_size = client.length, success_size = server.length, metadata_size = 0;
  memcpy(stale_request, client.captured, request_size);
  memcpy(stale_success, server.captured, success_size);
  CHECK(wl_kernel_rpc_write(stale_reject + 4, sizeof(stale_reject) - 4,
    old.instance, old.operation, 7, &metadata_size) == WL_OK);
  CHECK(wl_kernel_native_none_seal(stale_reject, sizeof(stale_reject), RESPONSE_MESSAGE_ID, metadata_size) == WL_OK);
  CHECK(source_reads == reads && close_owner(&client) == 0);
  source_mode = 3; identity = old.instance;
  CHECK(init(&client, source, false) == WL_ERR_INVALID_STATE);
  source_mode = 0; identity += 100;
  CHECK(init(&client, source, false) == WL_OK);
  CHECK(call(50, &current) == WL_OK && current.operation == old.operation && current.instance != old.instance);
  CHECK(wl_kernel_cancel(&client.endpoint.core, old) == WL_ERR_NOT_FOUND);
  CHECK(inject(&client.port, stale_success, success_size) == WL_OK);
  CHECK(inject(&client.port, stale_reject, metadata_size + 4) == WL_OK);
  CHECK(session_kernel_step(&client.endpoint, now) == WL_OK && notifications == 1);
  /* Requests are not replay-deduplicated. The old request executes again, but
   * its response cannot complete a call from the new instance. */
  CHECK(inject(&server.port, stale_request, request_size) == WL_OK);
  CHECK(drain() == 0 && notifications == 2 && executed == 3);
  CHECK(result.outcome == WL_KERNEL_SUCCESS && owned.output == 51);
  reads = source_reads;
  CHECK(call(-1, &current) == WL_OK && drain() == 0);
  CHECK(result.outcome == WL_KERNEL_REJECTED && result.status == 7);
  for (unsigned i = 0; i < 100; ++i) {
    CHECK(call((int)i, &current) == WL_OK && drain() == 0);
    CHECK(result.outcome == WL_KERNEL_SUCCESS && owned.output == (int32_t)i + 1);
  }
  CHECK(source_reads == reads);
  /* Test-only private-state seed: production exposes no operation-ID knob. */
  client.endpoint.core.operation = UINT32_MAX - 1;
  CHECK(call(80, &old) == WL_OK && old.operation == UINT32_MAX && drain() == 0);
  unsigned count = notifications;
  CHECK(call(90, &current) == WL_ERR_ID_EXHAUSTED && notifications == count);
  CHECK(close_owner(&client) == 0 && owned.output == 81);
  CHECK(init(&client, source, false) == WL_OK);
  CHECK(call(90, &current) == WL_OK && current.operation == 1 && current.instance != old.instance);
  CHECK(wl_kernel_cancel(&client.endpoint.core, old) == WL_ERR_NOT_FOUND);
  CHECK(drain() == 0 && notifications == count + 1 && owned.output == 91);
  CHECK(close_owner(&client) == 0 && close_owner(&server) == 0 && owned.output == 91);
  printf("SESSION fresh/old-success/old-reject/replayed-request/exhaustion hot_reads=0 endpoint_bytes=%u PASS\n",
    (unsigned)sizeof(client.endpoint));
  /* Native source initialization is explicit outer work too. */
  sentinel = 0;
  wl_err_t error = wl_session_next(native, 0, &sentinel);
  if (error == WL_OK) {
    CHECK(init(&client, native, false) == WL_OK);
    uint64_t previous = client.previous;
    CHECK(close_owner(&client) == 0 && init(&client, native, false) == WL_OK);
    CHECK(client.previous != previous && close_owner(&client) == 0);
    puts("SESSION platform endpoint reinit PASS");
  } else CHECK(error == WL_ERR_NOT_SUPPORTED);
  return 0;
}
