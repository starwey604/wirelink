/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "quickstart_runtime.h"
#include "wirelink/loopback.h"

#define CHECK(expression) do { \
  if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
    return 1; \
  } \
} while (0)

typedef struct {
  quickstart_endpoint_t *device;
  wl_time_ms_t now_ms;
} calculator_t;

static int32_t handle_add(void *context, const add_request_t *request,
                          const quickstart_add_request_token_t *token,
                          wl_delivery_t delivery) {
  calculator_t *calculator = context;
  add_response_t response;
  const int64_t sum = (int64_t)request->left + request->right;
  (void)delivery;
  if (sum < INT32_MIN || sum > INT32_MAX) {
    /* Business rejection: no fabricated sum or status field is needed. */
    return quickstart_endpoint_add_reject(calculator->device, token, 1,
                                          calculator->now_ms);
  }
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)sum;
  /* Preparing a response is synchronous; endpoint_step sends it later. */
  return quickstart_endpoint_add_complete(calculator->device, token,
                                          &response, calculator->now_ms);
}

int main(void) {
  static quickstart_endpoint_t controller, device;
  quickstart_endpoint_config_t client_config, server_config;
  calculator_t calculator = {&device, 0U};
  wl_loopback_t cable;
  telemetry_t telemetry, received;
  add_request_t request;
  quickstart_add_result_t operation;
  quickstart_add_call_t call;

  CHECK(quickstart_endpoint_config_defaults(&client_config, 0x1001U) == WL_OK);
  CHECK(quickstart_endpoint_config_defaults(&server_config, 0x2002U) == WL_OK);
  CHECK(quickstart_runtime_config_enable_client(&client_config.runtime) == WL_OK);
  CHECK(quickstart_runtime_config_enable_server(&server_config.runtime) == WL_OK);
  client_config.link.ack_timeout_ms = 20U;
  client_config.link.max_retries = 2U;
  server_config.link.ack_timeout_ms = 20U;
  server_config.link.max_retries = 2U;
  server_config.runtime.rpc_server_pending_timeout_ms = 1000U;
  server_config.runtime.rpc_server_cache_ttl_ms = 10000U;
  server_config.runtime.add_request_handler = handle_add;
  server_config.runtime.add_user_data = &calculator;
  CHECK(quickstart_endpoint_init_config(&controller, &client_config) == WL_OK);
  CHECK(quickstart_endpoint_init_config(&device, &server_config) == WL_OK);
  CHECK(wl_loopback_connect(&cable, quickstart_endpoint_handle(&controller),
                           quickstart_endpoint_handle(&device)) == WL_OK);

  telemetry_clear(&telemetry);
  telemetry.has_sample = true;
  telemetry.sample = 7U;
  telemetry.has_temperature_centi_c = true;
  telemetry.temperature_centi_c = 2350;
  CHECK(quickstart_endpoint_send_telemetry(&device, &telemetry).domain == QUICKSTART_SEND_OK);
  CHECK(quickstart_endpoint_step(&device, 1U) == WL_OK);
  CHECK(quickstart_endpoint_step(&controller, 1U) == WL_OK);
  CHECK(quickstart_endpoint_read_telemetry(&controller, &received) == WL_OK);
  CHECK(received.sample == 7U && received.temperature_centi_c == 2350);

  add_request_clear(&request);
  request.has_left = true;
  request.left = 20;
  request.has_right = true;
  request.right = 22;
  CHECK(quickstart_endpoint_add_call(&controller, &request, 100U, 10U,
                                    &call) == WL_RPC_OK);

  /* Simulated milliseconds. Real applications use their monotonic clock. */
  for (calculator.now_ms = 10U; calculator.now_ms < 30U; ++calculator.now_ms) {
    CHECK(quickstart_endpoint_step(&controller, calculator.now_ms) == WL_OK);
    CHECK(quickstart_endpoint_step(&device, calculator.now_ms) == WL_OK);
  }
  CHECK(quickstart_endpoint_add_inspect(&controller, &call, &operation) == WL_RPC_OK);
  CHECK(operation.state == WL_RPC_CLIENT_COMPLETED);
  CHECK(operation.response_valid && operation.response.sum == 42);
  CHECK(quickstart_endpoint_add_release(&controller, &call) == WL_RPC_OK);

  quickstart_endpoint_close(&controller);
  quickstart_endpoint_close(&device);
  puts("unreliable telemetry: sample=7 temperature=23.50 C");
  puts("reliable RPC: 20 + 22 = 42");
  return 0;
}
