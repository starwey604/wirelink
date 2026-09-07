/* SPDX-License-Identifier: Apache-2.0 */
#include SESSION_HEADER
#include "wirelink/port.h"
#include <stdio.h>
#include <string.h>
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#define P(suffix) CAT(SESSION_PREFIX,suffix)
#define STR_(x) #x
#define STR(x) STR_(x)
#define CHECK(x) do { if (!(x)) { printf("SESSION_P0 %s line=%d: %s FAIL\n", STR(SESSION_PREFIX), __LINE__, #x); return 1; } } while (0)

static P(_endpoint_t) client, server;
static uint64_t identity = UINT64_C(0x123456789abcdef0);
static unsigned source_reads, notifications, executed, mismatches;
static wl_time_ms_t now;
static int outcome, value, mode;
static P(_execute_request_token_t) deferred;
typedef struct {
  wl_ctx_t *peer;
  uint8_t response[96];
  size_t length;
  bool drop;
  uint8_t queue[4][96];
  size_t sizes[4];
  unsigned queued;
} transport_t;
static transport_t client_tx, server_tx;

static wl_time_ms_t clock_now(void *context) { (void)context; return now; }
static wl_err_t next_identity(void *context, uint64_t *out) {
  (void)context; ++source_reads;
  if (mode == 1) return WL_ERR_IO;
  *out = mode == 2 ? 0U : mode == 3 ? identity : ++identity;
  return WL_OK;
}
static wl_environment_t environment(void) {
  wl_environment_t env = {{clock_now, NULL}, {next_identity, NULL}};
  return env;
}
static uint64_t session(P(_endpoint_t) *endpoint) {
  return wl_link_session_id(wl_endpoint_link(P(_endpoint_handle)(endpoint)));
}
static wl_sink_result_t send(void *context, wl_io_token_t token,
                             const uint8_t *data, size_t length) {
  transport_t *tx = context;
  wl_frame_view_t view;
  (void)token;
  if (wl_frame_decode(data, length, WL_INTEGRITY_CRC32C, &view) != WL_OK)
    return WL_SINK_FAILED;
  if (view.type == WL_PACKET_DATA && view.message_id == RESPONSE_MESSAGE_ID) {
    if (length > sizeof(tx->response)) return WL_SINK_FAILED;
    memcpy(tx->response, data, length); tx->length = length;
    if (tx->drop) return WL_SINK_SENT;
  }
  if (tx->queued == 4U || length > sizeof(tx->queue[0])) return WL_SINK_BUSY;
  memcpy(tx->queue[tx->queued], data, length);
  tx->sizes[tx->queued++] = length;
  return WL_SINK_SENT;
}
static void transfer(transport_t *tx) {
  if (tx->queued != 0U && wl_feed_unit(tx->peer, tx->queue[0], tx->sizes[0]) == WL_OK) {
    --tx->queued;
    memmove(tx->queue, tx->queue + 1, tx->queued * sizeof(tx->queue[0]));
    memmove(tx->sizes, tx->sizes + 1, tx->queued * sizeof(tx->sizes[0]));
  }
}
static void observe(void *context, const P(_runtime_result_t) *result) {
  (void)context;
  if (result->detail_kind == CAT(SESSION_UPPER,_RUNTIME_DETAIL_RPC) &&
      result->detail.rpc.rpc_result == WL_RPC_ERR_SESSION_MISMATCH) ++mismatches;
}
static void completed(void *context, const wl_rpc_completion_t *result,
                      const response_value_t *response) {
  (void)context; ++notifications; outcome = result->status;
  if (response != NULL) value = response->output;
}
static int32_t execute(void *context, const request_value_t *request, response_value_t *response) {
  (void)context; ++executed;
  if (request->input < 0) return 7;
  response->has_output = true; response->output = request->input + 1;
  return 0;
}
static int32_t delayed(void *context, const request_t *request,
                       const P(_execute_request_token_t) *token, wl_delivery_t delivery) {
  (void)context; (void)request; (void)delivery;
  ++executed; deferred = *token; return 0;
}
static int attach(void) {
  client_tx.peer = wl_endpoint_link(P(_endpoint_handle)(&server));
  server_tx.peer = wl_endpoint_link(P(_endpoint_handle)(&client));
  CHECK(wl_set_sink(wl_endpoint_link(P(_endpoint_handle)(&client)), send, &client_tx) == WL_OK);
  CHECK(wl_set_sink(wl_endpoint_link(P(_endpoint_handle)(&server)), send, &server_tx) == WL_OK);
  return 0;
}
static int drain(unsigned count) {
  for (unsigned i = 0; i < count; ++i) {
    transfer(&server_tx);
    CHECK(P(_endpoint_step)(&client) == WL_OK);
    transfer(&client_tx);
    CHECK(P(_endpoint_step)(&server) == WL_OK);
    ++now;
  }
  return 0;
}
static int call(int input) {
  request_value_t request;
  request_value_clear(&request); request.has_input = true; request.input = input;
  CHECK(P(_endpoint_execute_async)(&client, &request, 100U, completed, NULL, NULL) == WL_OK);
  return 0;
}
static int inject(const uint8_t *data, size_t length, wl_err_t expected) {
  CHECK(wl_feed_unit(wl_endpoint_link(P(_endpoint_handle)(&client)), data, length) == WL_OK);
  CHECK(P(_endpoint_step)(&client) == expected);
  return 0;
}

int P(_run)(void) {
  P(_endpoint_config_t) config;
  uint8_t stale[96];
  size_t stale_size;
  unsigned reads;
  CHECK(P(_endpoint_config_defaults)(&config, environment()) == WL_OK);
  config.link.ack_timeout_ms = 20U;
  CHECK(source_reads == 0U); /* defaults do not consume an identity */
  mode = 1;
  CHECK(P(_endpoint_init_config)(&client, &config) == WL_ERR_IO);
  mode = 2;
  CHECK(P(_endpoint_init_config)(&client, &config) == WL_ERR_INVALID_STATE);
  CHECK(wl_endpoint_link(P(_endpoint_handle)(&client)) == NULL);
  mode = 0;
  config.on_result = observe;
  CHECK(P(_endpoint_init_config)(&client, &config) == WL_OK);
  const uint64_t first_session = session(&client);
  config.on_execute = execute;
  CHECK(P(_endpoint_init_config)(&server, &config) == WL_OK);
  CHECK(first_session != session(&server)); /* reuse SAME environment/config */
  CHECK(attach() == 0);
  reads = source_reads;
  CHECK(call(40) == 0 && drain(10) == 0);
  CHECK(notifications == 1 && outcome == WL_RPC_SUCCESS && value == 41 && executed == 1);
  stale_size = server_tx.length; memcpy(stale, server_tx.response, stale_size);
  CHECK(source_reads == reads);
  CHECK(P(_endpoint_close)(&client) == WL_OK);
  /* Rebuild only the client; server remains alive with its original identity. */
  config.on_execute = NULL;
  mode = 3; identity = first_session;
  CHECK(P(_endpoint_init_config)(&client, &config) == WL_ERR_INVALID_STATE);
  mode = 0; identity += 100U;
  CHECK(P(_endpoint_init_config)(&client, &config) == WL_OK);
  CHECK(session(&client) != first_session && attach() == 0);
  reads = source_reads;
  CHECK(call(50) == 0); /* operation 1 reused, same service, new client */
  CHECK(inject(stale, stale_size, WL_OK) == 0);
  CHECK(notifications == 1 && mismatches == 1); /* no accidental completion */
  CHECK(drain(10) == 0);
  if (notifications != 2 || value != 51 || executed != 2)
    printf("rebuild diagnostics: notifications=%u value=%d executed=%u outcome=%d now=%u\n",
        notifications, value, executed, outcome, now);
  CHECK(notifications == 2 && value == 51 && executed == 2);
  CHECK(source_reads == reads);

  /* A rejected response carries the same protection, without a business body. */
  CHECK(call(-1) == 0 && drain(10) == 0);
  CHECK(notifications == 3 && outcome == WL_RPC_REJECTED);
  stale_size = server_tx.length; memcpy(stale, server_tx.response, stale_size);
  CHECK(P(_endpoint_close)(&client) == WL_OK);
  CHECK(P(_endpoint_init_config)(&client, &config) == WL_OK && attach() == 0);
  /* Force the same numeric operation as the earlier rejection, not merely an unknown ID. */
  CHECK(call(1) == 0 && drain(10) == 0);
  CHECK(call(60) == 0);
  CHECK(inject(stale, stale_size, WL_OK) == 0);
  CHECK(notifications == 4 && mismatches == 2);
  CHECK(drain(10) == 0 && notifications == 5 && value == 61);

  /* Explicitly opt into deferred replies; token/cache must echo request identity. */
  CHECK(P(_endpoint_close)(&server) == WL_OK);
  config.advanced.execute_request_handler = delayed;
  CHECK(P(_endpoint_init_config)(&server, &config) == WL_OK && attach() == 0);
  reads = source_reads;
  CHECK(call(70) == 0 && drain(4) == 0 && notifications == 5);
  response_value_t response;
  response_value_clear(&response); response.has_output = true; response.output = 71;
  CHECK(P(_execute_server_complete_value)(P(_endpoint_runtime)(&server), &deferred, &response, now).domain == 0);
  CHECK(drain(10) == 0 && notifications == 6 && value == 71);
  CHECK(source_reads == reads);
  CHECK(P(_endpoint_close)(&client) == WL_OK && P(_endpoint_close)(&server) == WL_OK);
  config.advanced.execute_request_handler = NULL;
  config.advanced.rpc_client_next_operation_id = UINT32_MAX;
  CHECK(P(_endpoint_init_config)(&client, &config) == WL_OK);
  config.on_execute = execute;
  CHECK(P(_endpoint_init_config)(&server, &config) == WL_OK && attach() == 0);
  CHECK(call(80) == 0 && drain(10) == 0 && notifications == 7 && value == 81);
  request_value_t exhausted;
  request_value_clear(&exhausted); exhausted.has_input = true; exhausted.input = 90;
  CHECK(P(_endpoint_execute_async)(&client, &exhausted, 100U, completed, NULL, NULL) == WL_ERR_ID_EXHAUSTED);
  CHECK(notifications == 7);
  CHECK(P(_endpoint_close)(&client) == WL_OK);
  config.advanced.rpc_client_next_operation_id = 1U;
  config.on_execute = NULL;
  CHECK(P(_endpoint_init_config)(&client, &config) == WL_OK && attach() == 0);
  CHECK(call(90) == 0 && drain(10) == 0 && notifications == 8 && value == 91);
  CHECK(P(_endpoint_close)(&client) == WL_OK && P(_endpoint_close)(&server) == WL_OK);
  printf("SESSION_P0 %s rebuild/success/reject/deferred source_reads=%u hot_reads=0 endpoint_bytes=%u PASS\n",
      STR(SESSION_PREFIX), source_reads, (unsigned)sizeof(client));
  return 0;
}
