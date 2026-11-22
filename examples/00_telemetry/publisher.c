/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry.h"
#include "tutorial_host.h"
#include <wirelink/asio/packet_udp.h>
#include <inttypes.h>

static pub_node_t publisher;

/* [docs:telemetry_tx_write] */
static wl_asio_udp_t *open_publisher(int argc, char **argv) {
  wl_asio_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50001,
    .peer_address = "127.0.0.1", .peer_port = 50002,
    .maximum_tx_datagram_size = PUB_TX_WIRE_MAX
  };
  if (!example_udp_args(argc, argv, &config)) return NULL;
  wl_asio_udp_t *udp = wl_asio_udp_create(&publisher.port);
  if (!udp) return NULL;
  const wl_packet_io_t io = wl_asio_udp_io(udp);
  if (pub_init_io(&publisher, 0U, &io) != WL_OK ||
      wl_asio_udp_start(udp, &config) != WL_OK) {
    wl_asio_udp_destroy(udp);
    return NULL;
  }
  example_install_signals();
  return udp;
}
/* [/docs:telemetry_tx_write] */

/* [docs:telemetry_publish] */
static wl_err_t publish_sample(wl_asio_udp_t *udp, uint32_t sequence) {
  telemetry_t sample = {.sample = sequence, .temperature_centi_c = -1234};
  wl_err_t error = pub_send_telemetry(&publisher, &sample);
  if (error != WL_OK) return error;
  return wl_asio_udp_step_endpoint(udp, &publisher.core, 0U);
}
/* [/docs:telemetry_publish] */

int main(int argc, char **argv) {
  wl_asio_udp_t *udp = open_publisher(argc, argv);
  if (udp == NULL) return 1;
  int status = 0;
  uint32_t sequence = 0, next = example_now_ms();
  while (example_running()) {
    const uint32_t now = example_now_ms();
    if ((int32_t)(now - next) >= 0) {
      if (publish_sample(udp, ++sequence) != WL_OK) { status = 1; break; }
      printf("publish sample=%" PRIu32 "\n", sequence);
      fflush(stdout);
      next = now + 200U;
    } else if (wl_asio_udp_step_endpoint(udp, &publisher.core, 0U) != WL_OK ||
               wl_asio_udp_wait_endpoint(udp, &publisher.core, now, next - now) != WL_OK) {
      status = 1;
      break;
    }
  }
  if (wl_asio_udp_close_endpoint(udp, &publisher.core, example_now_ms()) != WL_OK) status = 1;
  wl_asio_udp_destroy(udp);
  return status;
}
