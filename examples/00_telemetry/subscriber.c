/* SPDX-License-Identifier: Apache-2.0 */
#include "telemetry_endpoint.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static telemetry_endpoint_t subscriber;
  uint16_t local = 49001, peer = 49000;
  telemetry_t value;
  int complete = 0;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(telemetry_endpoint_init(&subscriber, example_session_id(), example_clock()) == WL_OK);
  example_udp_t *udp = example_udp_open(telemetry_endpoint_handle(&subscriber), local, peer);
  CHECK(udp != NULL);
  puts("telemetry subscriber ready");
  fflush(stdout);

  const wl_time_ms_t started = example_now_ms();
  while (example_running() && (wl_time_ms_t)(example_now_ms() - started) < 10000U) {
    CHECK(telemetry_endpoint_step(&subscriber) == WL_OK);
    const int result = telemetry_endpoint_read_telemetry(&subscriber, &value);
    if (result == WL_OK) {
      printf("latest sample=%u temperature=%.2f C\n", (unsigned)value.sample,
             value.temperature_centi_c / 100.0);
      if (value.sample >= 5) { complete = 1; break; }
    } else {
      CHECK(result == WL_ERR_NO_DATA);
    }
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  example_udp_close(udp);
  if (!complete) fputs("no final sample received (UDP telemetry may be lost)\n", stderr);
  return complete ? 0 : 1;
}
