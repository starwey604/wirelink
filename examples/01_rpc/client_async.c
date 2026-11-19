/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include "example_check.h"
static unsigned completed;
static void done(client_node_t *self, wl_rpc_result_t result, const add_response_t *response) {
  (void)self;
  EXAMPLE_REQUIRE(result.outcome == WL_RPC_SUCCESS && response && response->sum == 42);
  ++completed;
}
/* [docs:calculator_async_rpc] */
void calculator_async_example(client_node_t *client) {
  /* Four accepted calls coexist; request values are copied during admission. */
  for (int32_t i = 0; i < 4; ++i) {
    EXAMPLE_REQUIRE(client_call_add(client, &(add_request_t){
        .left = i, .right = 42 - i}, done) == WL_OK);
  }
}
/* [/docs:calculator_async_rpc] */
unsigned calculator_async_count(void) { return completed; }
