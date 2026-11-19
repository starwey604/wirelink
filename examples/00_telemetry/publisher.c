/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry.h"
#include "example_check.h"
extern unsigned telemetry_received;
extern telemetry_t latest_telemetry;
static pub_node_t publisher;
static sub_node_t subscriber;

/* [docs:telemetry_tx_write] */
wl_err_t pub_tx_write(pub_node_t *self, const uint8_t *data, size_t length) {
  (void)self;
  return sub_rx_packet(&subscriber, data, length);
}
/* [/docs:telemetry_tx_write] */

/* [docs:telemetry_publish] */
static wl_err_t publish_sample(uint32_t sample) {
  wl_err_t error = pub_send_telemetry(&publisher, &(telemetry_t){
      .sample = sample, .temperature_centi_c = -1234});
  if (error != WL_OK) return error;
  error = pub_step(&publisher, 0);
  if (error != WL_OK) return error;
  return sub_step(&subscriber, 0);
}
/* [/docs:telemetry_publish] */

int main(void) {
  if (pub_init(&publisher) != WL_OK || sub_init(&subscriber) != WL_OK) return 1;
  for (unsigned sample = 0; sample < 12; ++sample) {
    if (publish_sample(sample) != WL_OK) return 1;
    EXAMPLE_REQUIRE(telemetry_received == sample + 1);
    EXAMPLE_REQUIRE(latest_telemetry.sample == sample &&
                    latest_telemetry.temperature_centi_c == -1234);
  }
  EXAMPLE_REQUIRE(pub_close(&publisher) == WL_OK);
  EXAMPLE_REQUIRE(sub_close(&subscriber) == WL_OK);
  EXAMPLE_REQUIRE(pub_quiescent(&publisher) && sub_quiescent(&subscriber));
  puts("12 ordinary telemetry messages received; both endpoints quiescent");
  return 0;
}
