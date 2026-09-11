/* SPDX-License-Identifier: Apache-2.0 */
#include "udp_demo_endpoint.h"
#include "network.h"

static udp_demo_endpoint_t client;
WL_ZEPHYR_UDP_DEFINE(udp, UDP_DEMO_ENDPOINT_MAX_PAYLOAD, 2);

int main(void) {
  if (sample_network_init() < 0) { printk("network unavailable\n"); return 1; }
  wl_err_t error = udp_demo_endpoint_init(&client, wl_platform_environment());
  if (error != WL_OK) { printk("endpoint init: %d\n", error); return 1; }
  error = sample_udp_open(&udp, udp_demo_endpoint_handle(&client));
  if (error != WL_OK) {
    printk("UDP open: %d\n", error);
    (void)udp_demo_endpoint_close(&client);
    return 1;
  }
  add_request_value_t request;
  add_request_value_clear(&request);
  request.has_left = request.has_right = true;
  request.left = 20;
  request.right = 22;
  add_response_value_t response;
  wl_rpc_completion_t result = udp_demo_endpoint_add_sync(&client, &request, &response, 1500);
  int status = 1;
  if (result.status == WL_RPC_SUCCESS) {
    printk("20 + 22 = %ld\n", (long)response.sum);
    uint32_t started = k_uptime_get_32();
    while ((uint32_t)(k_uptime_get_32() - started) < 500) {
      error = udp_demo_endpoint_step(&client);
      if (error != WL_OK) break;
      telemetry_t sample;
      if (udp_demo_endpoint_read_telemetry(&client, &sample) == WL_OK) {
        printk("telemetry sample=%lu\n", (unsigned long)sample.sample);
        status = response.sum == 42 ? 0 : 1;
        break;
      }
      error = wl_zephyr_udp_wait(&udp, 100);
      if (error != WL_OK && error != WL_ERR_NO_DATA) break;
    }
  } else printk("RPC: %s\n", wl_rpc_status_str(result.status));
  if (udp_demo_endpoint_close(&client) != WL_OK) status = 1;
  if (wl_zephyr_udp_close(&udp) != WL_OK) status = 1;
  return status;
}
