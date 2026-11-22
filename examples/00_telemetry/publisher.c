/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry.h"
#include "tutorial_host.h"
#include <wirelink/udp.h>
#include <inttypes.h>

static pub_node_t publisher;

/* [docs:telemetry_tx_write] */
static wl_udp_t *open_publisher(int argc, char **argv) {
  wl_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50001,
    .peer_address = "127.0.0.1", .peer_port = 50002
  };
  if (!example_udp_args(argc, argv, &config)) return NULL;
  wl_udp_t *udp = NULL;
  if (wl_udp_open(&udp, &config, NULL) != WL_OK) return NULL;
  if (pub_bind(&publisher, wl_udp_transport(udp)) != WL_OK) {
    wl_udp_close(udp);
    return NULL;
  }
  example_install_signals();
  return udp;
}
/* [/docs:telemetry_tx_write] */

/* [docs:telemetry_publish] */
static wl_err_t publish_sample(uint32_t sequence) {
  telemetry_t sample = {.sample = sequence, .temperature_centi_c = -1234};
  wl_err_t error = pub_send_telemetry(&publisher, &sample);
  if (error != WL_OK) return error;
  return wl_node_step(&publisher.node, 0U);
}
/* [/docs:telemetry_publish] */

int main(int argc, char **argv) {
  wl_udp_t *udp = open_publisher(argc, argv);
  if (udp == NULL) return 1;
  int status = 0;
  uint32_t sequence = 0, next = example_now_ms();
  while (example_running()) {
    const uint32_t now = example_now_ms();
    if ((int32_t)(now - next) >= 0) {
      if (publish_sample(++sequence) != WL_OK) { status = 1; break; }
      printf("publish sample=%" PRIu32 "\n", sequence);
      fflush(stdout);
      next = now + 200U;
    } else if (wl_node_step(&publisher.node, 0U) != WL_OK ||
               wl_node_wait(&publisher.node, now, next - now) != WL_OK) {
      status = 1;
      break;
    }
  }
  if (wl_node_close(&publisher.node, example_now_ms()) != WL_OK) status = 1;
  wl_udp_close(udp);
  return status;
}
