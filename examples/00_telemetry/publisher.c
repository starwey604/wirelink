/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry_kernel.h"
#include "loopback_owner.h"
extern unsigned telemetry_received;
void telemetry_receive(void *, const telemetry_t *);
static telemetry_kernel_t endpoints[2];
static example_pair_t pair;
int main(void) {
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  const telemetry_kernel_handlers_t handlers = {.on_message_telemetry = telemetry_receive};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(telemetry_kernel_init(&endpoints[side], &pair.ports[side], &handlers, NULL, NULL) == WL_OK);
  for (unsigned sample = 0; sample < 12; ++sample) {
    telemetry_value_t message = {.has_sample = true, .sample = sample,
      .has_temperature_centi_c = true, .temperature_centi_c = -1234};
    EXAMPLE_REQUIRE(telemetry_kernel_send_telemetry(&endpoints[0], &message) == WL_OK);
    memset(&message, 0, sizeof(message)); /* Admission copied the source. */
    example_pair_wait(&pair, &telemetry_received, sample + 1);
  }
  example_pair_close(&pair);
  puts("12 ordinary telemetry messages received; both endpoints quiescent");
  return 0;
}
