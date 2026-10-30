/* SPDX-License-Identifier: Apache-2.0 */
#include "device_service.h"
#include "self_test.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static device_server_endpoint_t endpoint;
  device_service_t device;
  self_test_job_t job = {0};
  device_server_endpoint_config_t config;
  uint16_t local = 49301, peer = 49300;
  CHECK(example_ports(argc, argv, &local, &peer));
  device_service_init(&device);
  job.endpoint = &endpoint;
  CHECK(device_server_endpoint_config_defaults(&config, wl_platform_environment(), wl_link_defaults()) == WL_OK);
  config.user_data = &device; /* Once for all ordinary handlers. */
  device_service_bind(&config);
  calculator_service_bind(&config);
  self_test_bind(&config, &job);
  CHECK(device_server_endpoint_init_config(&endpoint, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(device_server_endpoint_handle(&endpoint), local, peer);
  CHECK(udp != NULL);

  puts("device service server ready");
  fflush(stdout);
  device_telemetry_t telemetry;
  device_telemetry_clear(&telemetry);
  telemetry.has_sequence = telemetry.has_channels = true;
  wl_time_ms_t next_telemetry;
  CHECK(wl_endpoint_now(device_server_endpoint_handle(&endpoint), &next_telemetry) == WL_OK);
  while (example_running()) {
    CHECK(device_server_endpoint_step(&endpoint) == WL_OK);
    wl_time_ms_t now;
    CHECK(wl_endpoint_now(device_server_endpoint_handle(&endpoint), &now) == WL_OK);
    CHECK(self_test_poll(&job, now) == WL_RPC_OK);
    if ((int32_t)(now - next_telemetry) >= 0) {
      ++telemetry.sequence;
      for (unsigned i = 0; i < 100; ++i) telemetry.channels[i] = i + 1000;
      const device_send_result_t sent = device_server_endpoint_send_device_telemetry(&endpoint, &telemetry);
      CHECK(sent.domain == DEVICE_SEND_OK ||
            (sent.domain == DEVICE_SEND_CORE_ERROR && sent.core_result == WL_ERR_BUSY));
      next_telemetry = now + 20U;
    }
    uint32_t maximum = next_telemetry - now;
    if (job.pending) {
      uint32_t remaining = (int32_t)(now - job.due) >= 0 ? 0U : job.due - now;
      if (remaining < maximum) maximum = remaining;
    }
    CHECK(example_udp_wait(udp, maximum) == WL_OK);
  }
  CHECK(device_server_endpoint_close(&endpoint) == WL_OK);
  example_udp_close(udp);
  return 0;
}
