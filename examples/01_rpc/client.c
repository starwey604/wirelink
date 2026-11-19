/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include "example_check.h"
#include <limits.h>
static client_node_t caller;
static server_node_t callee;
static unsigned completed;
static wl_rpc_result_t result;
wl_err_t client_tx_write(client_node_t *self, const uint8_t *data, size_t length) {
  (void)self;
  return server_rx_packet(&callee, data, length);
}
wl_err_t server_tx_write(server_node_t *self, const uint8_t *data, size_t length) {
  (void)self;
  return client_rx_packet(&caller, data, length);
}
static wl_err_t advance(uint32_t now) {
  /* A receiver releasing space makes the opposite physical sink writable. */
  if (wl_packet_rx_available(&callee.port)) client_tx_ready(&caller);
  if (wl_packet_rx_available(&caller.port)) server_tx_ready(&callee);
  const wl_err_t error = client_step(&caller, now);
  if (error != WL_OK) return error;
  return server_step(&callee, 0);
}
/* [docs:calculator_rejected_callback] */
static void rejected(client_node_t *self, wl_rpc_result_t outcome, const add_response_t *response) {
  (void)self;
  if (response != NULL) return;
  result = outcome;
  ++completed;
}
/* [/docs:calculator_rejected_callback] */
int main(void) {
  EXAMPLE_REQUIRE(client_init(&caller) == WL_OK && server_init(&callee) == WL_OK);
  if (calculator_async_example(&caller) != WL_OK) return 1;
  for (unsigned pass = 0; pass < 100 && calculator_async_count() != 4; ++pass) {
    if (advance(pass) != WL_OK) return 1;
  }
  EXAMPLE_REQUIRE(calculator_async_count() == 4);
  /* [docs:calculator_sync_rpc] */
  if (client_call_add(&caller, &(add_request_t){
      .left = INT32_MAX, .right = 1}, rejected) != WL_OK) return 1;
  for (unsigned pass = 0; pass < 100 && !completed; ++pass) {
    if (advance(100 + pass) != WL_OK) return 1;
  }
  /* [/docs:calculator_sync_rpc] */
  EXAMPLE_REQUIRE(completed == 1 && result.outcome == WL_RPC_REJECTED && result.status == 1);
  EXAMPLE_REQUIRE(client_close(&caller) == WL_OK && server_close(&callee) == WL_OK);
  if (advance(200) != WL_OK) return 1;
  EXAMPLE_REQUIRE(client_quiescent(&caller) && server_quiescent(&callee));
  puts("four concurrent copied requests and business rejection passed");
  return 0;
}
