/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry.h"
#include "tutorial_host.h"
#include <inttypes.h>

static pub_node_t publisher;

/* [docs:telemetry_tx_write] */
static example_udp_t *open_publisher(int argc, char **argv) {
  example_udp_t *udp = example_udp_create(&publisher.port);
  if (udp == NULL) return NULL;
  if (pub_init_io(&publisher, 0U, example_udp_io(udp)) != WL_OK ||
      example_udp_start(udp, PUB_TX_WIRE_MAX, argc, argv, 50001, 50002) != WL_OK) {
    example_udp_destroy(udp);
    return NULL;
  }
  return udp;
}
/* [/docs:telemetry_tx_write] */

/* [docs:telemetry_publish] */
static wl_err_t publish_sample(example_udp_t *udp, uint32_t sequence) {
  telemetry_t sample = {.sample = sequence, .temperature_centi_c = -1234};
  wl_err_t error = pub_send_telemetry(&publisher, &sample);
  if (error != WL_OK) return error;
  return example_udp_step(udp, &publisher.core, 0U);
}
/* [/docs:telemetry_publish] */

int main(int argc, char **argv) {
  example_udp_t *udp = open_publisher(argc, argv);
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
    } else if (example_udp_step(udp, &publisher.core, 0U) != WL_OK ||
               example_udp_wait(udp, &publisher.core, next - now) != WL_OK) {
      status = 1;
      break;
    }
  }
  if (example_udp_close(udp, &publisher.core) != WL_OK) status = 1;
  return status;
}
