/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "calculator_runtime.h"
#include "tutorial_host.h"

static int32_t add(void *context, const add_request_t *request,
                   const calculator_add_request_token_t *token, wl_delivery_t delivery) {
  calculator_endpoint_t *server = context;
  const int64_t sum = (int64_t)request->left + request->right;
  (void)delivery;
  printf("handling %ld + %ld\n", (long)request->left, (long)request->right);
  fflush(stdout);
  if (sum < INT32_MIN || sum > INT32_MAX)
    return calculator_endpoint_add_reject(server, token, 1);
  add_response_t response;
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)sum;
  return calculator_endpoint_add_complete(server, token, &response);
}

int main(int argc, char **argv) {
  static calculator_endpoint_t server;
  calculator_endpoint_config_t config;
  uint16_t local = 49101, peer = 49100;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(calculator_endpoint_config_defaults(&config, example_session_id()) == WL_OK);
  config.clock = example_clock();
  CHECK(calculator_runtime_config_enable_server(&config.runtime) == WL_OK);
  config.link.ack_timeout_ms = 100U;
  config.link.max_retries = 4U;
  config.runtime.rpc_server_pending_timeout_ms = 1000U;
  config.runtime.rpc_server_cache_ttl_ms = 10000U;
  config.runtime.add_request_handler = add;
  config.runtime.add_user_data = &server;
  CHECK(calculator_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("calculator server ready");
  fflush(stdout);
  while (example_running()) {
    CHECK(calculator_endpoint_step(&server) == WL_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  example_udp_close(udp);
  return 0;
}
