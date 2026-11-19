/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info.h"
#include "example_check.h"
#include <string.h>
static info_client_node_t caller;
static info_server_node_t callee;
static unsigned completed;
static info_response_t result;
wl_err_t info_client_tx_write(info_client_node_t *self, const uint8_t *data, size_t length) {
  (void)self; return info_server_rx_packet(&callee, data, length);
}
wl_err_t info_server_tx_write(info_server_node_t *self, const uint8_t *data, size_t length) {
  (void)self; return info_client_rx_packet(&caller, data, length);
}
/* [docs:device_info_done] */
static void done(info_client_node_t *self, wl_kernel_result_t outcome, const info_response_t *response) {
  (void)self;
  EXAMPLE_REQUIRE(outcome.outcome == WL_KERNEL_SUCCESS && response);
  result = *response;
  ++completed;
}
/* [/docs:device_info_done] */
int main(void) {
  unsigned queries = 0;
  EXAMPLE_REQUIRE(info_client_init(&caller) == WL_OK && info_server_init(&callee) == WL_OK);
  callee.user = &queries;
  /* [docs:device_info_client] */
  for (unsigned call = 0; call < 8; ++call) {
    EXAMPLE_REQUIRE(info_client_call_get_info(&caller, &(info_request_t){0}, done) == WL_OK);
    EXAMPLE_REQUIRE(info_client_step(&caller, call) == WL_OK);
    EXAMPLE_REQUIRE(info_server_step(&callee, call) == WL_OK);
    EXAMPLE_REQUIRE(info_client_step(&caller, call) == WL_OK);
    EXAMPLE_REQUIRE(completed == call + 1 && result.query_count == call + 1);
  }
  EXAMPLE_REQUIRE(info_client_close(&caller) == WL_OK && info_server_close(&callee) == WL_OK);
  EXAMPLE_REQUIRE(info_client_quiescent(&caller) && info_server_quiescent(&callee));
  EXAMPLE_REQUIRE(result.name.length == 11 && memcmp(result.name.data, "workbench\0x", 11) == 0);
  /* [/docs:device_info_client] */
  puts("bounded UTF-8/NUL response copied before callback exit and retained after close");
  return 0;
}
