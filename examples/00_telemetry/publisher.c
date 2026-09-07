/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry_endpoint.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static telemetry_endpoint_t publisher;
  uint16_t local = 49000, peer = 49001;
  telemetry_t value;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(telemetry_endpoint_init(&publisher, wl_platform_environment()) == WL_OK);
  example_udp_t *udp = example_udp_open(telemetry_endpoint_handle(&publisher), local, peer);
  CHECK(udp != NULL);

  telemetry_clear(&value);
  value.has_sample = true;
  value.has_temperature_centi_c = true;
  value.temperature_centi_c = 2350;
  for (value.sample = 1; value.sample <= 5 && example_running(); ++value.sample) {
    CHECK(telemetry_endpoint_send_telemetry(&publisher, &value).domain == TELEMETRY_SEND_OK);
    printf("published sample=%u temperature=23.50 C\n", (unsigned)value.sample);
    /* Publish every 200 ms; keep servicing the endpoint while waiting. */
    const wl_time_ms_t started = example_now_ms();
    while ((wl_time_ms_t)(example_now_ms() - started) < 200U && example_running()) {
      CHECK(telemetry_endpoint_step(&publisher) == WL_OK);
      const uint32_t elapsed = example_now_ms() - started;
      if (elapsed < 200U) CHECK(example_udp_wait(udp, 200U - elapsed) == WL_OK);
    }
  }
  example_udp_close(udp);
  return 0;
}
