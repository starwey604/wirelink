/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_endpoint.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static calculator_endpoint_t client;
  add_response_value_t response;
  add_request_value_t request;
  uint16_t local = 49100, peer = 49101;
  CHECK(argc == 1 || argc == 3 || argc == 5);
  CHECK(example_ports(argc == 5 ? 3 : argc, argv, &local, &peer));
  add_request_value_clear(&request);
  request.has_left = request.has_right = true;
  request.left = 20;
  request.right = 22;
  if (argc == 5) {
    CHECK(example_int32(argv[3], &request.left));
    CHECK(example_int32(argv[4], &request.right));
  }

  CHECK(calculator_endpoint_init(&client, wl_platform_environment()) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&client), local, peer);
  CHECK(udp != NULL);
  const wl_rpc_completion_t result = calculator_endpoint_add_sync(&client, &request, &response, 1500U);
  CHECK(calculator_endpoint_close(&client) == WL_OK);
  if (result.status == WL_RPC_SUCCESS) {
    printf("%ld + %ld = %ld\n", (long)request.left, (long)request.right, (long)response.sum);
  } else if (result.status == WL_RPC_REJECTED) {
    printf("addition rejected: status=%ld\n", (long)result.rejection);
  } else {
    fprintf(stderr, "RPC failed: %s (local=%s)\n", wl_rpc_status_str(result.status), wl_err_str(result.local_error));
  }
  example_udp_close(udp);
  return result.status == WL_RPC_SUCCESS || result.status == WL_RPC_REJECTED ? 0 : 1;
}
