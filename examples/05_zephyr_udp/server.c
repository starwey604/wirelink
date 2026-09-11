/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "udp_demo_endpoint.h"
#include "peer_options.h"

static int32_t add(void *context, const add_request_value_t *request,
                   add_response_value_t *response) {
  (void)context;
  int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->has_sum = true;
  response->sum = (int32_t)sum;
  return 0;
}

int main(int argc, char **argv) {
  static udp_demo_endpoint_t server;
  udp_demo_endpoint_config_t config;
  CHECK(udp_demo_endpoint_config_defaults(&config, wl_platform_environment()) == WL_OK);
  config.on_add = add;
  CHECK(udp_demo_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = peer_open(udp_demo_endpoint_handle(&server), argc, argv);
  if (udp == NULL) {
    (void)udp_demo_endpoint_close(&server);
    return 1;
  }
  puts("UDP peer server ready");
  fflush(stdout);
  uint32_t sequence = 0;
  wl_time_ms_t due = example_now_ms();
  int status = 0;
  while (example_running()) {
    if (udp_demo_endpoint_step(&server) != WL_OK) { status = 1; break; }
    wl_time_ms_t now = example_now_ms();
    if ((int32_t)(now - due) >= 0) {
      telemetry_t sample;
      telemetry_clear(&sample);
      sample.has_sample = true;
      sample.sample = ++sequence;
      udp_demo_send_result_t sent = udp_demo_endpoint_send_telemetry(&server, &sample);
      if (sent.domain != UDP_DEMO_SEND_OK && sent.core_result != WL_ERR_BUSY) {
        status = 1;
        break;
      }
      due = now + 20; /* Business sampling period; never wait to fill a batch. */
    }
    now = example_now_ms();
    uint32_t wait = (int32_t)(due - now) > 0 ? due - now : 0;
    if (example_udp_wait(udp, wait) != WL_OK) { status = 1; break; }
  }
  if (udp_demo_endpoint_close(&server) != WL_OK) status = 1;
  example_udp_close(udp);
  return status;
}
