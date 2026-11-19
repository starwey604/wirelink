/* SPDX-License-Identifier: Apache-2.0 */
#include "bridge.h"
#include "calculator_fixture.h"
#include "loopback_owner.h"
#include "wirelink/clock.h"
#include <limits.h>

wl_clock_t clock_bridge_native_clock(void);
typedef struct {
  calculator_node_t client, server;
  example_pair_t pair;
  wl_rpc_result_t result;
  add_response_t response;
  wl_clock_t native;
  uint32_t time, reads, handled;
  bool ready, active, done, stall_peer;
} bridge_t;
static uint32_t read_clock(bridge_t *bridge) {
  ++bridge->reads;
  return bridge->native.now_ms ? bridge->native.now_ms(bridge->native.user_data) : bridge->time;
}
static int32_t add(void *user, const add_request_t *request, add_response_t *response) {
  bridge_t *bridge = user; ++bridge->handled;
  int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->sum = (int32_t)sum; return 0;
}
static void completed(void *user, wl_rpc_result_t result, const add_response_t *response) {
  bridge_t *bridge = user; bridge->result = result;
  if (result.outcome == WL_RPC_SUCCESS) {
    EXAMPLE_REQUIRE(response != NULL);
    bridge->response = *response;
  }
  bridge->done = true;
}
void *clock_bridge_create(uint32_t initial_ms, int native_clock) {
  bridge_t *bridge = calloc(1, sizeof(*bridge)); /* Only this test shim allocates. */
  if (!bridge) return NULL;
  bridge->time = initial_ms;
  if (native_clock) bridge->native = clock_bridge_native_clock();
  example_pair_init(&bridge->pair, &bridge->client.core, &bridge->server.core);
  calculator_handlers_t handlers = {.on_add = add};
  const calculator_handlers_t client_handlers = {0};
  EXAMPLE_REQUIRE(calculator_init(&bridge->client, &bridge->pair.ports[0], &client_handlers, bridge) == WL_OK);
  EXAMPLE_REQUIRE(calculator_init(&bridge->server, &bridge->pair.ports[1], &handlers, bridge) == WL_OK);
  bridge->ready = true; return bridge;
}
void clock_bridge_close(void *value) {
  bridge_t *bridge = value;
  if (!bridge || !bridge->ready) return;
  example_pair_close(&bridge->pair); bridge->ready = false;
}
void clock_bridge_destroy(void *value) { clock_bridge_close(value); free(value); }
int clock_bridge_start(void *value, int32_t left, int32_t right, uint32_t timeout_ms) {
  bridge_t *bridge = value;
  if (!bridge || !bridge->ready || (bridge->active && !bridge->done)) return -1;
  add_request_t request = {.left = left, .right = right};
  bridge->pair.now = read_clock(bridge); wl_call_handle_t handle;
  if (calculator_call_add_ex(&bridge->client, &request, bridge->pair.now, timeout_ms,
      completed, bridge, &handle) != WL_OK) return -1;
  /* Test transport/owner stall replaces the removed deferred reply capability. */
  bridge->stall_peer = left == INT32_MIN;
  bridge->active = true; bridge->done = false; memset(&request, 0, sizeof(request)); return 0;
}
int clock_bridge_step(void *value) {
  bridge_t *bridge = value;
  if (!bridge || !bridge->ready) return -1;
  const uint32_t tick = read_clock(bridge); bridge->pair.now = tick;
  unsigned reads = bridge->reads;
  if (calculator_step(&bridge->client, tick) != WL_OK) return -1;
  if (!bridge->stall_peer && calculator_step(&bridge->server, tick) != WL_OK) return -1;
  if (wl_packet_loopback_service(&bridge->pair.cable, 2) != WL_OK) return -1;
  EXAMPLE_REQUIRE(bridge->reads == reads); /* Both kernels perform zero reads. */
  return 0;
}
int clock_bridge_advance(void *value, uint32_t delta_ms) {
  bridge_t *bridge = value;
  if (!bridge || !bridge->ready || bridge->native.now_ms) return -1;
  bridge->time += delta_ms; return 0;
}
int clock_bridge_result(void *value, int32_t *sum) {
  bridge_t *bridge = value;
  if (!bridge || !bridge->active || !sum) return -1;
  if (!bridge->done) return 0;
  switch (bridge->result.outcome) {
    case WL_RPC_SUCCESS: *sum = bridge->response.sum; return 1;
    case WL_RPC_REJECTED: return 2;
    case WL_RPC_TIMED_OUT: return 3;
    default: return -1;
  }
}
uint32_t clock_bridge_reads(const void *value) { const bridge_t *bridge = value; return bridge ? bridge->reads : 0; }
uint32_t clock_bridge_handled(const void *value) { const bridge_t *bridge = value; return bridge ? bridge->handled : 0; }
