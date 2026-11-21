/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info.h"
#include "tutorial_host.h"
#include <inttypes.h>

static unsigned completed;
static wl_rpc_result_t result;
static info_response_t saved;

/* [docs:device_info_done] */
static void done(info_client_node_t *self, wl_rpc_result_t outcome, const info_response_t *response) {
  (void)self;
  result = outcome;
  if (response != NULL) saved = *response;
  ++completed;
}
/* [/docs:device_info_done] */

/* [docs:device_info_client] */
static wl_err_t read_device_info(info_client_node_t *client, example_udp_t *udp) {
  wl_err_t error = info_client_call_get_info(client, &(info_request_t){0}, done);
  while (error == WL_OK && example_running() && !completed) {
    error = example_udp_step(udp, &client->core, example_now_ms());
    if (error == WL_OK && !completed)
      error = example_udp_wait(udp, &client->core, 100U);
  }
  return completed || error != WL_OK ? error : WL_ERR_CANCELLED;
}
/* [/docs:device_info_client] */

int main(int argc, char **argv) {
  static info_client_node_t client;
  example_udp_t *udp = example_udp_create(&client.port);
  if (udp == NULL) return 1;
  if (info_client_init_io(&client, 0U, example_udp_io(udp)) != WL_OK ||
      example_udp_start(udp, INFO_CLIENT_TX_WIRE_MAX, argc, argv, 50005, 50006) != WL_OK) {
    example_udp_destroy(udp);
    return 1;
  }
  const wl_err_t read = read_device_info(&client, udp);
  const wl_err_t closed = example_udp_close(udp, &client.core);
  if (read != WL_OK || closed != WL_OK || result.outcome != WL_RPC_SUCCESS) return 1;
  fputs("name=", stdout);
  fwrite(saved.name.data, 1, saved.name.length, stdout);
  printf(" firmware=%.*s query_count=%" PRIu32 "\n",
         (int)saved.firmware.length, saved.firmware.data, saved.query_count);
  return 0;
}
