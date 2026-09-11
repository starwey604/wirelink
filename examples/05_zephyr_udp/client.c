/* SPDX-License-Identifier: Apache-2.0 */
#include "udp_demo_endpoint.h"
#include "peer_options.h"

int main(int argc, char **argv) {
  static udp_demo_endpoint_t client;
  CHECK(udp_demo_endpoint_init(&client, wl_platform_environment()) == WL_OK);
  example_udp_t *udp = peer_open(udp_demo_endpoint_handle(&client), argc, argv);
  if (udp == NULL) {
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
    printf("20 + 22 = %ld\n", (long)response.sum);
    /* Observe a telemetry update as well; no synthetic timestamps in calls. */
    wl_time_ms_t started = example_now_ms();
    while (example_running() && (uint32_t)(example_now_ms() - started) < 500) {
      if (udp_demo_endpoint_step(&client) != WL_OK) break;
      telemetry_t sample;
      if (udp_demo_endpoint_read_telemetry(&client, &sample) == WL_OK) {
        printf("telemetry sample=%lu\n", (unsigned long)sample.sample);
        status = response.sum == 42 ? 0 : 1;
        break;
      }
      if (example_udp_wait(udp, 100) != WL_OK) break;
    }
  } else fprintf(stderr, "RPC: %s\n", wl_rpc_status_str(result.status));
  if (udp_demo_endpoint_close(&client) != WL_OK) status = 1;
  example_udp_close(udp);
  return status;
}
