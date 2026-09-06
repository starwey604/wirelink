/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_endpoint.h"
#include "tutorial_host.h"

typedef struct {
  bool done;
  wl_rpc_completion_t result;
  add_response_value_t response;
} addition_t;

static void completed(void *context, const wl_rpc_completion_t *result,
                       const add_response_value_t *response) {
  addition_t *addition = context;
  addition->result = *result;
  if (response != NULL) addition->response = *response;
  addition->done = true;
}

int main(int argc, char **argv) {
  static calculator_endpoint_t client;
  addition_t addition = {0};
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

  CHECK(calculator_endpoint_init(&client, example_session_id(), example_clock()) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&client), local, peer);
  CHECK(udp != NULL);
  CHECK(calculator_endpoint_add_async(&client, &request, 1500U,
      completed, &addition, NULL) == WL_OK);

  while (!addition.done && example_running()) {
    const int step = calculator_endpoint_step(&client);
    if (step != WL_OK) fprintf(stderr, "endpoint: %s\n", wl_err_str(step));
    if (!addition.done) CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(calculator_endpoint_close(&client) == WL_OK); /* Also completes a call interrupted by Ctrl-C. */
  if (addition.result.status == WL_RPC_SUCCESS) {
    printf("%ld + %ld = %ld\n", (long)request.left, (long)request.right, (long)addition.response.sum);
  } else if (addition.result.status == WL_RPC_REJECTED) {
    printf("addition rejected: status=%ld\n", (long)addition.result.rejection);
  } else {
    fprintf(stderr, "RPC failed: %s\n", wl_rpc_status_str(addition.result.status));
  }
  example_udp_close(udp);
  return addition.result.status == WL_RPC_SUCCESS ||
         addition.result.status == WL_RPC_REJECTED ? 0 : 1;
}
