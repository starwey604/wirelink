/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry.h"
#include "tutorial_host.h"
#include <wirelink/udp.h>
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
  wl_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50002,
    .peer_address = "127.0.0.1", .peer_port = 50001
  };
  if (!example_udp_args(argc, argv, &config)) return 1;
  wl_udp_t *udp = NULL;
  if (wl_udp_open(&udp, &config, NULL) != WL_OK) return 1;
  if (sub_bind(&subscriber, wl_udp_transport(udp)) != WL_OK) {
    wl_udp_close(udp);
    return 1;
  }
  example_install_signals();
  int status = 0;
  const wl_clock_t clock = example_clock();
  while (example_running()) {
    if (wl_node_poll(&subscriber.node, clock, 100U) != WL_OK) {
      status = 1;
      break;
    }
  }
  if (wl_node_close(&subscriber.node, example_now_ms()) != WL_OK) status = 1;
  wl_udp_close(udp);
  return status;
}
