/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry.h"
#include "tutorial_host.h"
#include <wirelink/asio/packet_udp.h>
#include <inttypes.h>

/* [docs:telemetry_receive] */
void sub_on_telemetry(sub_node_t *self, const telemetry_t *message) {
  (void)self;
  printf("sample=%" PRIu32 " temperature_centi_c=%" PRId32 "\n",
         message->sample, message->temperature_centi_c);
  fflush(stdout);
}
/* [/docs:telemetry_receive] */

int main(int argc, char **argv) {
  static sub_node_t subscriber;
  wl_asio_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50002,
    .peer_address = "127.0.0.1", .peer_port = 50001,
    .maximum_tx_datagram_size = SUB_TX_WIRE_MAX
  };
  if (!example_udp_args(argc, argv, &config)) return 1;
  wl_asio_udp_t *udp = wl_asio_udp_create(&subscriber.port);
  if (!udp) return 1;
  const wl_packet_io_t io = wl_asio_udp_io(udp);
  if (sub_init_io(&subscriber, 0U, &io) != WL_OK ||
      wl_asio_udp_start(udp, &config) != WL_OK) {
    wl_asio_udp_destroy(udp);
    return 1;
  }
  example_install_signals();
  int status = 0;
  while (example_running()) {
    if (wl_asio_udp_step_endpoint(udp, &subscriber.core, 0U) != WL_OK ||
        wl_asio_udp_wait_endpoint(udp, &subscriber.core, example_now_ms(), 100U) != WL_OK) {
      status = 1;
      break;
    }
  }
  if (wl_asio_udp_close_endpoint(udp, &subscriber.core, example_now_ms()) != WL_OK) status = 1;
  wl_asio_udp_destroy(udp);
  return status;
}
