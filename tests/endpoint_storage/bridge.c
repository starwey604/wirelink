/* SPDX-License-Identifier: Apache-2.0 */
#include "bridge.h"
#include "../support/test_environment.h"
#include "calculator_endpoint.h"
#include "wirelink/storage/fixed_pool.h"
#include "wirelink/loopback.h"
#include <stdlib.h>

typedef struct {
  union { calculator_endpoint_t alignment; unsigned char bytes[2 * sizeof(calculator_endpoint_t)]; } storage;
  wl_fixed_pool_t pool;
  calculator_endpoint_t *client, *server;
  wl_loopback_t cable;
  uint32_t time;
  unsigned allocations, deallocations;
  int fail_second;
} bridge_t;

static void *allocate(void *context, size_t size, size_t alignment) {
  bridge_t *bridge = context;
  ++bridge->allocations;
  if (bridge->fail_second && bridge->allocations == 2) return NULL;
  wl_allocator_t pool = wl_fixed_pool_allocator(&bridge->pool);
  return pool.allocate(pool.context, size, alignment);
}
static void deallocate(void *context, void *pointer, size_t size, size_t alignment) {
  bridge_t *bridge = context;
  ++bridge->deallocations;
  wl_allocator_t pool = wl_fixed_pool_allocator(&bridge->pool);
  memset(pointer, 0xdd, size);
  pool.deallocate(pool.context, pointer, size, alignment);
}
static wl_time_ms_t now(void *context) { return ((bridge_t *)context)->time; }
static int32_t add(void *context, const add_request_value_t *request, add_response_value_t *response) {
  (void)context;
  response->has_sum = true;
  response->sum = request->left + request->right;
  return 0;
}
static int transfer(void *context) {
  bridge_t *bridge = context;
  wl_loopback_service_result_t result;
  const int error = wl_loopback_service(&bridge->cable, 4, &result);
  return error == WL_ERR_NO_DATA || error == WL_ERR_WOULD_BLOCK ? WL_OK : error;
}
static int service(void *context) {
  bridge_t *bridge = context;
  int error = transfer(bridge);
  if (error != WL_OK) return error;
  error = calculator_endpoint_step(bridge->server);
  return error == WL_OK ? transfer(bridge) : error;
}
static uint32_t hint(const void *context, wl_time_ms_t time) {
  const bridge_t *bridge = context;
  wl_adapter_stats_t a, b;
  wl_poll_hint_t peer;
  (void)wl_loopback_get_stats(&bridge->cable, WL_LOOPBACK_ENDPOINT_A, &a);
  (void)wl_loopback_get_stats(&bridge->cable, WL_LOOPBACK_ENDPOINT_B, &b);
  (void)wl_poll_get_hint(wl_endpoint_link(calculator_endpoint_handle(bridge->server)), time, &peer);
  return a.tx_active || b.tx_active || peer.work_pending ? 0U : peer.next_deadline_ms;
}
static void quiesce(void *context) { wl_loopback_quiesce(&((bridge_t *)context)->cable); }
static wl_err_t storage_wait(void *context, uint32_t maximum) {
  ((bridge_t *)context)->time += maximum;
  return WL_ERR_NO_DATA;
}

void *storage_bridge_create(int fail_second) {
  bridge_t *bridge = calloc(1, sizeof(*bridge)); /* Native binding object only. */
  calculator_endpoint_config_t config;
  if (bridge == NULL) return NULL;
  bridge->fail_second = fail_second;
  if (wl_fixed_pool_init(&bridge->pool, bridge->storage.bytes, sizeof(bridge->storage.bytes),
      sizeof(calculator_endpoint_t), CALCULATOR_ENDPOINT_ALIGNMENT, 2) != WL_OK) goto fail;
  wl_allocator_t allocator = {allocate, deallocate, bridge};
  if (calculator_endpoint_config_defaults(&config, test_environment_id(81, (wl_clock_t){0})) != WL_OK) goto fail;
  config.environment.clock = (wl_clock_t){now, bridge};
  if (calculator_endpoint_create(&bridge->client, &config, &allocator) != WL_OK) goto fail;
  config.on_add = add;
  if (calculator_endpoint_create(&bridge->server, &config, &allocator) != WL_OK) goto fail;
  if (wl_loopback_init(&bridge->cable, wl_endpoint_link(calculator_endpoint_handle(bridge->client)),
      wl_endpoint_link(calculator_endpoint_handle(bridge->server))) != WL_OK) goto fail;
  wl_pump_hooks_t hooks = {0};
  hooks.adapter_user_data = bridge;
  hooks.service = transfer;
  hooks.quiesce = quiesce;
  hooks.adapter_deadline_hint = hint;
  if (wl_endpoint_attach(calculator_endpoint_handle(bridge->server), &hooks) != WL_OK) goto fail;
  hooks.service = service;
  if (wl_endpoint_attach(calculator_endpoint_handle(bridge->client), &hooks) != WL_OK) goto fail;
  wl_waiter_t waiter = {storage_wait, bridge, NULL};
  if (wl_endpoint_set_waiter(calculator_endpoint_handle(bridge->client), &waiter) != WL_OK) goto fail;
  return bridge;
fail:
  storage_bridge_destroy(bridge);
  return NULL;
}
int storage_bridge_call(void *object, int32_t left, int32_t *sum) {
  bridge_t *bridge = object;
  if (bridge == NULL || bridge->client == NULL || sum == NULL) return -1;
  add_request_value_t request;
  add_response_value_t response;
  add_request_value_clear(&request);
  request.has_left = request.has_right = true;
  request.left = left; request.right = 0;
  const wl_rpc_completion_t result = calculator_endpoint_add_sync(bridge->client, &request, &response, 1000);
  if (result.status != WL_RPC_SUCCESS) return -1;
  *sum = response.sum;
  return 0;
}
int storage_bridge_close(void *object) {
  bridge_t *bridge = object;
  if (bridge == NULL) return 0;
  if (calculator_endpoint_destroy(&bridge->client) != WL_OK ||
      calculator_endpoint_destroy(&bridge->server) != WL_OK) return -1;
  return wl_fixed_pool_in_use(&bridge->pool) == 0 ? 0 : -1;
}
void storage_bridge_destroy(void *object) {
  if (storage_bridge_close(object) != 0) abort();
  free(object);
}
unsigned storage_bridge_allocations(void *object) { return ((bridge_t *)object)->allocations; }
unsigned storage_bridge_deallocations(void *object) { return ((bridge_t *)object)->deallocations; }
