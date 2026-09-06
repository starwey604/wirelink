/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_runtime.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static calculator_endpoint_t client;
  calculator_endpoint_config_t config;
  calculator_add_call_t call;
  calculator_add_result_t result;
  add_request_t request;
  uint16_t local = 49100, peer = 49101;
  CHECK(argc == 1 || argc == 3 || argc == 5);
  CHECK(example_ports(argc == 5 ? 3 : argc, argv, &local, &peer));
  add_request_clear(&request);
  request.has_left = request.has_right = true;
  request.left = 20;
  request.right = 22;
  if (argc == 5) {
    CHECK(example_int32(argv[3], &request.left));
    CHECK(example_int32(argv[4], &request.right));
  }

  CHECK(calculator_endpoint_config_defaults(&config, example_session_id()) == WL_OK);
  CHECK(calculator_runtime_config_enable_client(&config.runtime) == WL_OK);
  config.link.ack_timeout_ms = 100U;
  config.link.max_retries = 4U;
  CHECK(calculator_endpoint_init_config(&client, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&client), local, peer);
  CHECK(udp != NULL);
  /* The core's send clock comes from the latest owner step. */
  CHECK(calculator_endpoint_step(&client, example_now_ms()) == WL_OK);
  CHECK(calculator_endpoint_add_call(&client, &request, 1500U, example_now_ms(), &call) == WL_RPC_OK);

  for (;;) {
    const int step = calculator_endpoint_step(&client, example_now_ms());
    if (step != WL_OK) fprintf(stderr, "endpoint: %s\n", wl_err_str(step));
    CHECK(calculator_endpoint_add_inspect(&client, &call, &result) == WL_RPC_OK);
    if (result.state == WL_RPC_CLIENT_COMPLETED ||
        result.state == WL_RPC_CLIENT_APPLICATION_ERROR ||
        result.state == WL_RPC_CLIENT_TIMED_OUT ||
        result.state == WL_RPC_CLIENT_LINK_FAILED ||
        result.state == WL_RPC_CLIENT_CANCELLED) break;
    if (!example_running()) CHECK(calculator_endpoint_add_cancel(&client, &call) == WL_RPC_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  if (result.state == WL_RPC_CLIENT_COMPLETED && result.response_valid) {
    printf("%ld + %ld = %ld\n", (long)request.left, (long)request.right, (long)result.response.sum);
  } else if (result.state == WL_RPC_CLIENT_APPLICATION_ERROR) {
    printf("addition rejected: status=%ld\n", (long)result.application_status);
  } else {
    fprintf(stderr, "RPC failed: state=%ld\n", (long)result.state);
  }
  CHECK(calculator_endpoint_add_release(&client, &call) == WL_RPC_OK);
  example_udp_close(udp);
  return result.state == WL_RPC_CLIENT_COMPLETED ||
         result.state == WL_RPC_CLIENT_APPLICATION_ERROR ? 0 : 1;
}
