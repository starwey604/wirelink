/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "device_info_endpoint.h"
#include "tutorial_host.h"

static int32_t get_info(void *context, const info_request_value_t *request,
                        info_response_value_t *response) {
  uint32_t *queries = context;
  const char name[] = "demo-sensor";
  const char firmware[] = "dev";
  (void)request;
  response->has_name = response->has_firmware = response->has_query_count = true;
  response->name.length = sizeof(name) - 1U;
  memcpy(response->name.data, name, sizeof(name) - 1U);
  response->firmware.length = sizeof(firmware) - 1U;
  memcpy(response->firmware.data, firmware, sizeof(firmware) - 1U);
  response->query_count = ++*queries;
  return 0;
}

int main(int argc, char **argv) {
  static device_info_endpoint_t server;
  device_info_endpoint_config_t config;
  uint32_t queries = 0;
  uint16_t local = 49201, peer = 49200;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(device_info_endpoint_config_defaults(&config, wl_platform_environment()) == WL_OK);

  config.on_get_info = get_info;
  config.get_info_user_data = &queries;
  CHECK(device_info_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(device_info_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("device info server ready");
  fflush(stdout);
  while (example_running()) {
    CHECK(device_info_endpoint_step(&server) == WL_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(device_info_endpoint_close(&server) == WL_OK);
  example_udp_close(udp);
  return 0;
}
