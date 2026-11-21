/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry.h"
#include "tutorial_host.h"
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
  example_udp_t *udp = example_udp_create(&subscriber.port);
  if (udp == NULL) return 1;
  if (sub_init_io(&subscriber, 0U, example_udp_io(udp)) != WL_OK ||
      example_udp_start(udp, SUB_TX_WIRE_MAX, argc, argv, 50002, 50001) != WL_OK) {
    example_udp_destroy(udp);
    return 1;
  }
  int status = 0;
  while (example_running()) {
    if (example_udp_step(udp, &subscriber.core, 0U) != WL_OK ||
        example_udp_wait(udp, &subscriber.core, 100U) != WL_OK) {
      status = 1;
      break;
    }
  }
  if (example_udp_close(udp, &subscriber.core) != WL_OK) status = 1;
  return status;
}
