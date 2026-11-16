/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry.h"
#include "example_check.h"
/* [docs:telemetry_receive] */
unsigned telemetry_received;
void sub_on_telemetry(sub_node_t *self, const telemetry_t *message) {
  (void)self;
  EXAMPLE_REQUIRE(message->sample == telemetry_received);
  EXAMPLE_REQUIRE(message->temperature_centi_c == -1234);
  ++telemetry_received;
}
/* [/docs:telemetry_receive] */
