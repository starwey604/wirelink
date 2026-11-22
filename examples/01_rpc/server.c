/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator.h"
#include "tutorial_host.h"
#include <wirelink/asio/packet_udp.h>
#include <limits.h>
/* [docs:calculator_server] */
int32_t server_on_add(server_node_t *self, const add_request_t *request, add_response_t *response) {
  (void)self;
  const int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->sum = (int32_t)sum;
  return 0;
}
/* [/docs:calculator_server] */

/* [docs:calculator_udp_server] */
int main(int argc, char **argv) {
  static server_node_t server;
  wl_asio_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50004,
    .peer_address = "127.0.0.1", .peer_port = 50003,
    .maximum_tx_datagram_size = SERVER_TX_WIRE_MAX
  };
  if (!example_udp_args(argc, argv, &config)) return 1;
  wl_asio_udp_t *udp = wl_asio_udp_create(&server.port);
  if (!udp) return 1;
  const wl_packet_io_t io = wl_asio_udp_io(udp);
  if (server_init_io(&server, 0U, &io) != WL_OK ||
      wl_asio_udp_start(udp, &config) != WL_OK) {
    wl_asio_udp_destroy(udp);
    return 1;
  }
  example_install_signals();
  puts("Calculator server ready");
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
/* [/docs:calculator_udp_server] */
