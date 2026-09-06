/* SPDX-License-Identifier: Apache-2.0 */
#include "bridge.h"
#include "calculator_runtime.h"
#include "wirelink/loopback.h"
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

wl_clock_t clock_bridge_native_clock(void);

typedef struct {
  calculator_endpoint_t client, server;
  wl_loopback_t cable;
  calculator_add_call_t call;
  wl_clock_t native;
  uint32_t time, reads, handled;
  int ready, active;
} bridge_t;

static wl_time_ms_t read_clock(void *user) {
  bridge_t *bridge = user;
  ++bridge->reads;
  return bridge->native.now_ms ? bridge->native.now_ms(bridge->native.user_data)
                              : bridge->time;
}

static int32_t add(void *user, const add_request_t *request,
                   const calculator_add_request_token_t *token, wl_delivery_t delivery) {
  bridge_t *bridge = user;
  const int64_t sum = (int64_t)request->left + request->right;
  (void)delivery;
  ++bridge->handled;
  if (request->left == INT32_MIN) return 0; /* Deliberately defer test work. */
  if (sum < INT32_MIN || sum > INT32_MAX)
    return calculator_endpoint_add_reject(&bridge->server, token, 1);
  add_response_t response;
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)sum;
  return calculator_endpoint_add_complete(&bridge->server, token, &response);
}

void *clock_bridge_create(uint32_t initial_ms, int native_clock) {
  bridge_t *bridge = calloc(1, sizeof(*bridge)); /* Only the host test shim allocates. */
  calculator_endpoint_config_t client, server;
  if (!bridge) return NULL;
  bridge->time = initial_ms;
  if (native_clock) bridge->native = clock_bridge_native_clock();
  if (calculator_endpoint_config_defaults(&client, 1U) != WL_OK ||
      calculator_endpoint_config_defaults(&server, 2U) != WL_OK) goto failed;
  client.clock = server.clock = (wl_clock_t){read_clock, bridge};
  if (calculator_runtime_config_enable_client(&client.runtime) != WL_OK ||
      calculator_runtime_config_enable_server(&server.runtime) != WL_OK) goto failed;
  client.link.ack_timeout_ms = server.link.ack_timeout_ms = 50U;
  client.link.max_retries = server.link.max_retries = 2U;
  server.runtime.rpc_server_pending_timeout_ms = 1000U;
  server.runtime.rpc_server_cache_ttl_ms = 10000U;
  /* The native-clock scenario issues consecutive calls without waiting for
   * TTL. The manual-clock scenario keeps REJECT_NEW to test idle-gap expiry. */
  if (native_clock) server.runtime.rpc_server_cache_policy = WL_RPC_CACHE_EVICT_OLDEST;
  server.runtime.add_request_handler = add;
  server.runtime.add_user_data = bridge;
  if (calculator_endpoint_init_config(&bridge->client, &client) != WL_OK ||
      calculator_endpoint_init_config(&bridge->server, &server) != WL_OK) goto failed;
  if (wl_loopback_connect(&bridge->cable, calculator_endpoint_handle(&bridge->client),
                          calculator_endpoint_handle(&bridge->server)) != WL_OK) goto failed;
  bridge->ready = 1;
  return bridge;
failed:
  clock_bridge_destroy(bridge);
  return NULL;
}

void clock_bridge_close(void *value) {
  bridge_t *bridge = value;
  if (!bridge) return;
  calculator_endpoint_close(&bridge->client);
  calculator_endpoint_close(&bridge->server);
  bridge->ready = 0;
}

void clock_bridge_destroy(void *bridge) {
  clock_bridge_close(bridge);
  free(bridge);
}

int clock_bridge_start(void *value, int32_t left, int32_t right, uint32_t timeout_ms) {
  bridge_t *bridge = value;
  if (!bridge || !bridge->ready || bridge->active) return -1;
  add_request_t request;
  add_request_clear(&request);
  request.has_left = request.has_right = true;
  request.left = left;
  request.right = right;
  if (calculator_endpoint_add_call(&bridge->client, &request, timeout_ms,
                                   &bridge->call) != WL_RPC_OK) return -1;
  bridge->active = 1;
  return 0;
}

int clock_bridge_step(void *value) {
  bridge_t *bridge = value;
  if (!bridge || !bridge->ready) return -1;
  const int client = calculator_endpoint_step(&bridge->client);
  const int server = calculator_endpoint_step(&bridge->server);
  if (client != WL_OK || server != WL_OK) {
    const calculator_runtime_result_t *a = calculator_endpoint_result(&bridge->client);
    const calculator_runtime_result_t *b = calculator_endpoint_result(&bridge->server);
    fprintf(stderr, "clock bridge step: client=%d/%d/%d server=%d/%d/%d time=%u\n",
        client, (int)a->domain, (int)a->detail.rpc.rpc_result,
        server, (int)b->domain, (int)b->detail.rpc.rpc_result, bridge->time);
    return -1;
  }
  return 0;
}

int clock_bridge_advance(void *value, uint32_t delta_ms) {
  bridge_t *bridge = value;
  if (!bridge || !bridge->ready || bridge->native.now_ms) return -1;
  bridge->time += delta_ms;
  return 0;
}

int clock_bridge_result(void *value, int32_t *sum) {
  bridge_t *bridge = value;
  calculator_add_result_t result;
  if (!bridge || !bridge->ready || !bridge->active || !sum) return -1;
  if (calculator_endpoint_add_inspect(&bridge->client, &bridge->call, &result) != WL_RPC_OK)
    return -1;
  if (result.state == WL_RPC_CLIENT_COMPLETED && result.response_valid) {
    *sum = result.response.sum;
    return 1;
  }
  if (result.state == WL_RPC_CLIENT_APPLICATION_ERROR) return 2;
  if (result.state == WL_RPC_CLIENT_TIMED_OUT) return 3;
  return result.state == WL_RPC_CLIENT_QUEUED || result.state == WL_RPC_CLIENT_LINK_PENDING ||
         result.state == WL_RPC_CLIENT_WAIT_RESPONSE ? 0 : -1;
}

int clock_bridge_release(void *value) {
  bridge_t *bridge = value;
  if (!bridge || !bridge->ready || !bridge->active) return -1;
  if (calculator_endpoint_add_release(&bridge->client, &bridge->call) != WL_RPC_OK) return -1;
  bridge->active = 0;
  return 0;
}

uint32_t clock_bridge_reads(const void *value) {
  const bridge_t *bridge = value;
  return bridge ? bridge->reads : 0U;
}
uint32_t clock_bridge_handled(const void *value) {
  const bridge_t *bridge = value;
  return bridge ? bridge->handled : 0U;
}
