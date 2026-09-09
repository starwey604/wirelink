/* SPDX-License-Identifier: Apache-2.0 */
#include "device_client_endpoint.h"
#include "tutorial_host.h"
#include <limits.h>
#include <string.h>

static int check_device(device_client_endpoint_t *endpoint) {
  ping_request_value_t ping = {.has_nonce = true, .nonce = 1234};
  ping_response_value_t pong;
  CHECK(device_client_endpoint_ping_sync(endpoint, &ping, &pong, 1500).status == WL_RPC_SUCCESS);
  CHECK(pong.nonce == ping.nonce);

  get_info_request_value_t query;
  get_info_response_value_t info;
  get_info_request_value_clear(&query);
  CHECK(device_client_endpoint_get_info_sync(endpoint, &query, &info, 1500).status == WL_RPC_SUCCESS);
  const get_info_response_value_t saved = info;
  set_name_request_value_t rename;
  set_name_response_value_t renamed;
  set_name_request_value_clear(&rename);
  rename.has_name = true;
  rename.name.length = 7;
  memcpy(rename.name.data, "updated", 7);
  CHECK(device_client_endpoint_set_name_sync(endpoint, &rename, &renamed, 1500).status == WL_RPC_SUCCESS);
  CHECK(device_client_endpoint_get_info_sync(endpoint, &query, &info, 1500).status == WL_RPC_SUCCESS);
  CHECK(info.name.length == 7 && memcmp(info.name.data, "updated", 7) == 0);
  CHECK(saved.name.length == 9 && memcmp(saved.name.data, "workbench", 9) == 0);

  write_register_request_value_t write = {.has_index = true, .index = 2, .has_value = true, .value = 42};
  write_register_response_value_t written;
  CHECK(device_client_endpoint_write_register_sync(endpoint, &write, &written, 1500).status == WL_RPC_SUCCESS);
  read_register_request_value_t read = {.has_index = true, .index = 2};
  read_register_response_value_t value;
  CHECK(device_client_endpoint_read_register_sync(endpoint, &read, &value, 1500).status == WL_RPC_SUCCESS);
  CHECK(value.value == 42);
  read.index = 9;
  const wl_rpc_completion_t rejected = device_client_endpoint_read_register_sync(endpoint, &read, &value, 1500);
  CHECK(rejected.status == WL_RPC_REJECTED && rejected.rejection == 1);

  get_counters_request_value_t count;
  get_counters_response_value_t counters;
  reset_counters_request_value_t reset;
  reset_counters_response_value_t cleared;
  get_counters_request_value_clear(&count);
  reset_counters_request_value_clear(&reset);
  CHECK(device_client_endpoint_get_counters_sync(endpoint, &count, &counters, 1500).status == WL_RPC_SUCCESS);
  CHECK(counters.writes == 1);
  CHECK(device_client_endpoint_reset_counters_sync(endpoint, &reset, &cleared, 1500).status == WL_RPC_SUCCESS);
  CHECK(device_client_endpoint_get_counters_sync(endpoint, &count, &counters, 1500).status == WL_RPC_SUCCESS);
  CHECK(counters.writes == 0);
  puts("device RPCs: OK (owned string, state change, rejection)");
  return 0;
}

static int check_calculator(device_client_endpoint_t *endpoint) {
  add_request_value_t add = {.has_left = true, .left = 20, .has_right = true, .right = 22};
  add_response_value_t sum;
  CHECK(device_client_endpoint_add_sync(endpoint, &add, &sum, 1500).status == WL_RPC_SUCCESS);
  CHECK(sum.sum == 42);
  add.left = INT32_MAX;
  CHECK(device_client_endpoint_add_sync(endpoint, &add, &sum, 1500).status == WL_RPC_REJECTED);

  scale_request_value_t scale = {.has_value = true, .value = 7, .has_factor = true, .factor = 6};
  scale_response_value_t product;
  CHECK(device_client_endpoint_scale_sync(endpoint, &scale, &product, 1500).status == WL_RPC_SUCCESS);
  CHECK(product.value == 42);

  set_limits_request_value_t set = {.has_lower = true, .lower = -10, .has_upper = true, .upper = 10};
  set_limits_response_value_t accepted;
  get_limits_request_value_t get;
  get_limits_response_value_t limits;
  get_limits_request_value_clear(&get);
  CHECK(device_client_endpoint_set_limits_sync(endpoint, &set, &accepted, 1500).status == WL_RPC_SUCCESS);
  CHECK(device_client_endpoint_get_limits_sync(endpoint, &get, &limits, 1500).status == WL_RPC_SUCCESS);
  CHECK(limits.lower == -10 && limits.upper == 10);
  set.lower = 20;
  CHECK(device_client_endpoint_set_limits_sync(endpoint, &set, &accepted, 1500).status == WL_RPC_REJECTED);
  puts("calculator RPCs: OK");
  return 0;
}

typedef struct { unsigned completed; bool failed; } batch_t;
static void ping_done(void *context, const wl_rpc_completion_t *result, const ping_response_value_t *response) {
  batch_t *batch = context;
  batch->failed |= result->status != WL_RPC_SUCCESS || response == NULL || response->nonce != 99;
  ++batch->completed;
}
static void add_done(void *context, const wl_rpc_completion_t *result, const add_response_value_t *response) {
  batch_t *batch = context;
  batch->failed |= result->status != WL_RPC_SUCCESS || response == NULL || response->sum != 42;
  ++batch->completed;
}
static void scale_done(void *context, const wl_rpc_completion_t *result, const scale_response_value_t *response) {
  batch_t *batch = context;
  batch->failed |= result->status != WL_RPC_SUCCESS || response == NULL || response->value != 42;
  ++batch->completed;
}

int main(int argc, char **argv) {
  static device_client_endpoint_t endpoint;
  uint16_t local = 49300, peer = 49301;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(device_client_endpoint_init(&endpoint, wl_platform_environment()) == WL_OK);
  example_udp_t *udp = example_udp_open(device_client_endpoint_handle(&endpoint), local, peer);
  CHECK(udp != NULL);

  CHECK(check_device(&endpoint) == 0);
  CHECK(check_calculator(&endpoint) == 0);
  self_test_request_value_t test = {.has_pattern = true, .pattern = 123};
  self_test_response_value_t tested;
  CHECK(device_client_endpoint_self_test_sync(&endpoint, &test, &tested, 1500).status == WL_RPC_SUCCESS);
  CHECK(tested.observed == (test.pattern ^ UINT32_C(0xa5a5)));
  puts("deferred self-test: OK");

  batch_t batch = {0};
  ping_request_value_t ping = {.has_nonce = true, .nonce = 99};
  add_request_value_t add = {.has_left = true, .left = 40, .has_right = true, .right = 2};
  scale_request_value_t scale = {.has_value = true, .value = 21, .has_factor = true, .factor = 2};
  CHECK(device_client_endpoint_ping_async(&endpoint, &ping, 1500, ping_done, &batch, NULL) == WL_OK);
  CHECK(device_client_endpoint_add_async(&endpoint, &add, 1500, add_done, &batch, NULL) == WL_OK);
  CHECK(device_client_endpoint_scale_async(&endpoint, &scale, 1500, scale_done, &batch, NULL) == WL_OK);
  /* Requests were snapshotted on admission; no inspect/release loop. */
  ping.nonce = 0;
  add.left = 0;
  scale.value = 0;
  while (batch.completed != 3) {
    CHECK(device_client_endpoint_step(&endpoint) == WL_OK);
    CHECK(example_udp_wait(udp, 20) == WL_OK);
  }
  CHECK(!batch.failed);
  puts("mixed async batch: OK (3 unique completions)");

  device_telemetry_t telemetry;
  CHECK(device_client_endpoint_read_device_telemetry(&endpoint, &telemetry) == WL_OK);
  CHECK(telemetry.sequence != 0 && telemetry.channels[0] == 1000 && telemetry.channels[99] == 1099);
  puts("outbound-only telemetry: OK (100 channels)");

  /* extension: client */

  CHECK(device_client_endpoint_close(&endpoint) == WL_OK);
  example_udp_close(udp);
  puts("12 services: OK");
  return 0;
}
