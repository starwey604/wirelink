/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include "loopback_owner.h"
static unsigned completed;
static void done(void *user, wl_kernel_result_t result, const add_response_t *response) {
  (void)user;
  EXAMPLE_REQUIRE(result.outcome == WL_KERNEL_SUCCESS && response && response->sum == 42);
  ++completed;
}
void calculator_async_example(calculator_kernel_t *client, calculator_kernel_t *server) {
  /* Four accepted calls coexist; no terminal slot needs inspect/release. */
  (void)server;
  for (int32_t i = 0; i < 4; ++i) {
    add_request_value_t request = {.has_left = true, .left = i, .has_right = true, .right = 42 - i};
    wl_kernel_call_handle_t handle;
    EXAMPLE_REQUIRE(calculator_kernel_add_start(client, &request, 0, 500, done, NULL, &handle) == WL_OK);
    memset(&request, 0, sizeof(request));
  }
}
unsigned calculator_async_count(void) { return completed; }
