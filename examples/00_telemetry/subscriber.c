/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry_kernel.h"
#include "loopback_owner.h"
unsigned telemetry_received;
void telemetry_receive(void *user, const telemetry_t *message) {
  (void)user;
  EXAMPLE_REQUIRE(message->sample == telemetry_received);
  EXAMPLE_REQUIRE(message->temperature_centi_c == -1234);
  ++telemetry_received;
}
