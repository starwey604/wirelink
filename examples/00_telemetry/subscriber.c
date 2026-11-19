/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry.h"
/* [docs:telemetry_receive] */
unsigned telemetry_received;
telemetry_t latest_telemetry;

void sub_on_telemetry(sub_node_t *self, const telemetry_t *message) {
  (void)self;
  latest_telemetry = *message;
  ++telemetry_received;
}
/* [/docs:telemetry_receive] */
