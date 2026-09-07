/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_endpoint.h"
#include "../../support/test_environment.h"

static wl_time_ms_t now(void *context) { (void)context; return 1U; }

int main(void) {
  static calculator_endpoint_t endpoint;
  add_request_value_t request;
  add_response_value_t response;
  const wl_clock_t clock = {now, NULL};
  if (calculator_endpoint_init(&endpoint, test_environment_id(1, clock)) != WL_OK) return 1;
  add_request_value_clear(&request);
  request.has_left = request.has_right = true;
  request.left = 20; request.right = 22;
  response.sum = 99;
  const wl_rpc_completion_t result = calculator_endpoint_add_sync(&endpoint, &request, &response, 10);
  if (result.status != WL_RPC_FAILED || result.local_error != WL_ERR_NOT_SUPPORTED || response.sum != 99) return 2;
  return calculator_endpoint_close(&endpoint) == WL_OK ? 0 : 3;
}
