/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info.h"
#include "tutorial_host.h"
#include <wirelink/asio/packet_udp.h>
#include <string.h>
/* [docs:device_info_server] */
int32_t info_server_on_get_info(info_server_node_t *self, const info_request_t *request, info_response_t *response) {
  (void)request;
  unsigned *queries = self->user;
  response->name.length = 11; memcpy(response->name.data, "workbench\0x", 11);
  response->firmware.length = 6; memcpy(response->firmware.data, "0.0.99", 6);
  response->query_count = ++*queries;
  return 0;
}
/* [/docs:device_info_server] */

int main(int argc, char **argv) {
  static info_server_node_t server;
  wl_asio_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50006,
    .peer_address = "127.0.0.1", .peer_port = 50005,
    .maximum_tx_datagram_size = INFO_SERVER_TX_WIRE_MAX
  };
  if (!example_udp_args(argc, argv, &config)) return 1;
  wl_asio_udp_t *udp = wl_asio_udp_create(&server.port);
  if (!udp) return 1;
  const wl_packet_io_t io = wl_asio_udp_io(udp);
  if (info_server_init_io(&server, 0U, &io) != WL_OK ||
      wl_asio_udp_start(udp, &config) != WL_OK) {
    wl_asio_udp_destroy(udp);
    return 1;
  }
  example_install_signals();
  unsigned queries = 0;
  server.user = &queries;
  puts("Device info server ready");
  fflush(stdout);
  int status = 0;
  while (example_running()) {
    if (wl_asio_udp_step_endpoint(udp, &server.core, 0U) != WL_OK ||
        wl_asio_udp_wait_endpoint(udp, &server.core, example_now_ms(), 100U) != WL_OK) {
      status = 1;
      break;
    }
  }
  if (wl_asio_udp_close_endpoint(udp, &server.core, example_now_ms()) != WL_OK) status = 1;
  wl_asio_udp_destroy(udp);
  return status;
}
