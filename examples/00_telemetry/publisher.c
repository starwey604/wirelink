/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry.h"
#include "example_check.h"
extern unsigned telemetry_received;
static pub_node_t publisher;
static sub_node_t subscriber;

/* [docs:telemetry_tx_write] */
wl_err_t pub_tx_write(pub_node_t *self, const uint8_t *data, size_t length) {
  (void)self;
  return sub_rx_packet(&subscriber, data, length);
}
/* [/docs:telemetry_tx_write] */
int main(void) {
  /* [docs:telemetry_publish] */
  EXAMPLE_REQUIRE(pub_init(&publisher) == WL_OK);
  EXAMPLE_REQUIRE(sub_init(&subscriber) == WL_OK);
  for (unsigned sample = 0; sample < 12; ++sample) {
    EXAMPLE_REQUIRE(pub_send_telemetry(&publisher, &(telemetry_t){
        .sample = sample, .temperature_centi_c = -1234}) == WL_OK);
    EXAMPLE_REQUIRE(pub_step(&publisher, sample) == WL_OK);
    EXAMPLE_REQUIRE(sub_step(&subscriber, sample) == WL_OK);
    EXAMPLE_REQUIRE(telemetry_received == sample + 1);
  }
  /* [/docs:telemetry_publish] */
  EXAMPLE_REQUIRE(pub_close(&publisher) == WL_OK);
  EXAMPLE_REQUIRE(sub_close(&subscriber) == WL_OK);
  EXAMPLE_REQUIRE(pub_quiescent(&publisher) && sub_quiescent(&subscriber));
  puts("12 ordinary telemetry messages received; both endpoints quiescent");
  return 0;
}
