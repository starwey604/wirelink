/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info_endpoint.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static device_info_endpoint_t client;
  info_request_value_t request;
  info_response_value_t response;
  uint16_t local = 49200, peer = 49201;
  CHECK(example_ports(argc, argv, &local, &peer));
  info_request_value_clear(&request);
  CHECK(device_info_endpoint_init(&client, wl_platform_environment()) == WL_OK);
  example_udp_t *udp = example_udp_open(device_info_endpoint_handle(&client), local, peer);
  CHECK(udp != NULL);

  wl_rpc_completion_t result = device_info_endpoint_get_info_sync(&client, &request, &response, 1500U);
  if (result.status != WL_RPC_SUCCESS) {
    fprintf(stderr, "GetInfo failed: %s\n", wl_rpc_status_str(result.status));
    CHECK(device_info_endpoint_close(&client) == WL_OK);
    example_udp_close(udp);
    return 1;
  }
  const info_response_value_t saved = response; /* Copies the strings too. */
  result = device_info_endpoint_get_info_sync(&client, &request, &response, 1500U);
  CHECK(result.status == WL_RPC_SUCCESS);
  printf("current name=%.*s firmware=%.*s query=%lu\n",
         (int)response.name.length, response.name.data,
         (int)response.firmware.length, response.firmware.data,
         (unsigned long)response.query_count);
  info_response_value_clear(&response); /* Does not change saved. */
  CHECK(device_info_endpoint_close(&client) == WL_OK);
  example_udp_close(udp);

  /* These fields remain usable after another call and endpoint close. */
  printf("saved after close name=%.*s firmware=%.*s query=%lu\n",
         (int)saved.name.length, saved.name.data,
         (int)saved.firmware.length, saved.firmware.data,
         (unsigned long)saved.query_count);
  return 0;
}
