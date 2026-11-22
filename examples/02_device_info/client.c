/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info.h"
#include "tutorial_host.h"
#include <wirelink/asio/packet_udp.h>
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
static wl_err_t read_device_info(info_client_node_t *client, wl_asio_udp_t *udp) {
  wl_err_t error = info_client_call_get_info(client, &(info_request_t){0}, done);
  while (error == WL_OK && example_running() && !completed) {
    error = wl_asio_udp_step_endpoint(udp, &client->core, example_now_ms());
    if (error == WL_OK && !completed)
      error = wl_asio_udp_wait_endpoint(udp, &client->core, example_now_ms(), 100U);
  }
  return completed || error != WL_OK ? error : WL_ERR_CANCELLED;
}
/* [/docs:device_info_client] */

int main(int argc, char **argv) {
  static info_client_node_t client;
  wl_asio_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50005,
    .peer_address = "127.0.0.1", .peer_port = 50006,
    .maximum_tx_datagram_size = INFO_CLIENT_TX_WIRE_MAX
  };
  if (!example_udp_args(argc, argv, &config)) return 1;
  wl_asio_udp_t *udp = wl_asio_udp_create(&client.port);
  if (!udp) return 1;
  const wl_packet_io_t io = wl_asio_udp_io(udp);
  if (info_client_init_io(&client, 0U, &io) != WL_OK ||
      wl_asio_udp_start(udp, &config) != WL_OK) {
    wl_asio_udp_destroy(udp);
    return 1;
  }
  example_install_signals();
  const wl_err_t read = read_device_info(&client, udp);
  const wl_err_t closed = wl_asio_udp_close_endpoint(udp, &client.core, example_now_ms());
  wl_asio_udp_destroy(udp);
  if (read != WL_OK || closed != WL_OK || result.outcome != WL_RPC_SUCCESS) return 1;
  fputs("name=", stdout);
  fwrite(saved.name.data, 1, saved.name.length, stdout);
  printf(" firmware=%.*s query_count=%" PRIu32 "\n",
         (int)saved.firmware.length, saved.firmware.data, saved.query_count);
  return 0;
}
