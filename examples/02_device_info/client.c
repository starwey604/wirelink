/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info.h"
#include "tutorial_host.h"
#include <wirelink/udp.h>
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
static wl_err_t read_device_info(info_client_node_t *client) {
  const wl_clock_t clock = example_clock();
  wl_err_t error = info_client_call_get_info(client, &(info_request_t){0}, done);
  while (error == WL_OK && example_running() && !completed)
    error = wl_node_poll(&client->node, clock, 100U);
  return completed || error != WL_OK ? error : WL_ERR_CANCELLED;
}
/* [/docs:device_info_client] */

int main(int argc, char **argv) {
  static info_client_node_t client;
  wl_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50005,
    .peer_address = "127.0.0.1", .peer_port = 50006
  };
  if (!example_udp_args(argc, argv, &config)) return 1;
  wl_udp_t *udp = NULL;
  if (wl_udp_open(&udp, &config, NULL) != WL_OK) return 1;
  if (info_client_bind(&client, wl_udp_transport(udp)) != WL_OK) {
    wl_udp_close(udp);
    return 1;
  }
  example_install_signals();
  const wl_err_t read = read_device_info(&client);
  const wl_err_t closed = wl_node_close(&client.node, example_now_ms());
  wl_udp_close(udp);
  if (read != WL_OK || closed != WL_OK || result.outcome != WL_RPC_SUCCESS) return 1;
  fputs("name=", stdout);
  fwrite(saved.name.data, 1, saved.name.length, stdout);
  printf(" firmware=%.*s query_count=%" PRIu32 "\n",
         (int)saved.firmware.length, saved.firmware.data, saved.query_count);
  return 0;
}
