/* SPDX-License-Identifier: Apache-2.0 */
#include "bridge.h"
#include "calculator_kernel.h"
#include "wirelink/packet_loopback.h"
#include "wirelink/storage/fixed_pool.h"
#include "../support/test_environment.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
typedef struct {
  calculator_kernel_t endpoint;
  wl_packet_port_t port;
  uint8_t rx[2][2053];
} owner_t;
typedef struct {
  owner_t blocks[2];
  wl_fixed_pool_t pool;
  owner_t *client, *server;
  wl_packet_loopback_t cable;
  uint32_t time;
  unsigned allocations, deallocations;
  bool fail_second, initialized, done;
  wl_kernel_result_t result;
  int32_t sum;
} bridge_t;
static void *allocate(void *context, size_t size, size_t alignment) {
  bridge_t *bridge = context; ++bridge->allocations;
  if (bridge->fail_second && bridge->allocations == 2) return NULL;
  wl_allocator_t pool = wl_fixed_pool_allocator(&bridge->pool);
  return pool.allocate(pool.context, size, alignment);
}
static void deallocate(void *context, void *pointer, size_t size, size_t alignment) {
  bridge_t *bridge = context; ++bridge->deallocations;
  wl_allocator_t pool = wl_fixed_pool_allocator(&bridge->pool);
  memset(pointer, 0xdd, size); pool.deallocate(pool.context, pointer, size, alignment);
}
static int32_t add(void *user, const add_request_t *request, add_response_value_t *response) {
  (void)user; response->has_sum = true; response->sum = request->left; return 0;
}
static void completed(void *user, wl_kernel_result_t result, const add_response_t *response) {
  bridge_t *bridge = user; bridge->result = result;
  if (result.outcome == WL_KERNEL_SUCCESS) { CHECK(response); bridge->sum = response->sum; }
  bridge->done = true;
}
void *storage_bridge_create(int fail_second) {
  bridge_t *bridge = calloc(1, sizeof(*bridge)); /* Foreign test wrapper only. */
  if (!bridge) return NULL;
  bridge->fail_second = fail_second != 0;
  CHECK(wl_fixed_pool_init(&bridge->pool, bridge->blocks, sizeof(bridge->blocks),
    sizeof(owner_t), _Alignof(owner_t), 2) == WL_OK);
  bridge->client = allocate(bridge, sizeof(owner_t), _Alignof(owner_t));
  if (bridge->client) bridge->server = allocate(bridge, sizeof(owner_t), _Alignof(owner_t));
  if (!bridge->client || !bridge->server) { storage_bridge_destroy(bridge); return NULL; }
  CHECK(wl_packet_loopback_init(&bridge->cable, &bridge->client->port, &bridge->server->port, NULL, NULL) == WL_OK);
  owner_t *owners[2] = {bridge->client, bridge->server};
  for (unsigned side = 0; side < 2; ++side) {
    owner_t *owner = owners[side]; uint64_t instance;
    CHECK(wl_session_next((wl_session_source_t){test_next_session, NULL}, 0, &instance) == WL_OK);
    wl_packet_config_t packet = {.instance = instance, .rx_storage = &owner->rx[0][0],
      .rx_unit_size = sizeof(owner->rx[0]), .rx_storage_size = sizeof(owner->rx),
      .submit = wl_packet_loopback_submit, .notify = wl_packet_loopback_notify,
      .user = &bridge->cable.directions[side]};
    CHECK(wl_packet_init(&owner->port, &packet) == WL_OK);
    calculator_kernel_handlers_t handlers = {.on_rpc_add = add};
    CHECK(calculator_kernel_init(&owner->endpoint, &owner->port, &handlers, NULL, NULL) == WL_OK);
  }
  bridge->initialized = true; return bridge;
}
int storage_bridge_call(void *object, int32_t left, int32_t *sum) {
  bridge_t *bridge = object;
  if (!bridge || !bridge->initialized || !sum) return -1;
  add_request_value_t request = {.has_left = true, .left = left, .has_right = true, .right = 0};
  wl_kernel_call_handle_t handle; bridge->done = false;
  if (calculator_kernel_add_start(&bridge->client->endpoint, &request, bridge->time, 1000,
      completed, bridge, &handle) != WL_OK) return -1;
  memset(&request, 0, sizeof(request));
  for (unsigned pass = 0; pass < 1000 && !bridge->done; ++pass) {
    CHECK(calculator_kernel_step(&bridge->client->endpoint, bridge->time) == WL_OK);
    CHECK(calculator_kernel_step(&bridge->server->endpoint, bridge->time) == WL_OK);
    CHECK(wl_packet_loopback_service(&bridge->cable, 2) == WL_OK); ++bridge->time;
  }
  if (!bridge->done || bridge->result.outcome != WL_KERNEL_SUCCESS) return -1;
  *sum = bridge->sum; return 0;
}
int storage_bridge_close(void *object) {
  bridge_t *bridge = object;
  if (!bridge) return 0;
  if (bridge->initialized) {
    owner_t *owners[2] = {bridge->client, bridge->server};
    bool cancelled[2] = {bridge->cable.directions[0].pending != 0,
      bridge->cable.directions[1].pending != 0};
    for (unsigned side = 0; side < 2; ++side) CHECK(wl_kernel_close(&owners[side]->endpoint.core) == WL_OK);
    wl_packet_loopback_stop(&bridge->cable);
    for (unsigned side = 0; side < 2; ++side) {
      wl_err_t error = calculator_kernel_step(&owners[side]->endpoint, bridge->time);
      CHECK(error == WL_OK || (cancelled[side] && error == WL_ERR_IO));
      CHECK(wl_kernel_quiescent(&owners[side]->endpoint.core));
    }
    bridge->initialized = false;
  }
  if (bridge->client) { deallocate(bridge, bridge->client, sizeof(owner_t), _Alignof(owner_t)); bridge->client = NULL; }
  if (bridge->server) { deallocate(bridge, bridge->server, sizeof(owner_t), _Alignof(owner_t)); bridge->server = NULL; }
  return wl_fixed_pool_in_use(&bridge->pool) == 0 ? 0 : -1;
}
void storage_bridge_destroy(void *object) { CHECK(storage_bridge_close(object) == 0); free(object); }
unsigned storage_bridge_allocations(void *object) { return ((bridge_t *)object)->allocations; }
unsigned storage_bridge_deallocations(void *object) { return ((bridge_t *)object)->deallocations; }
