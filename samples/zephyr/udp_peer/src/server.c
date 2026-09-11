/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "udp_demo_endpoint.h"
#include "network.h"

static udp_demo_endpoint_t server;
WL_ZEPHYR_UDP_DEFINE(udp, UDP_DEMO_ENDPOINT_MAX_PAYLOAD, 2);

static int32_t add(void *context, const add_request_value_t *request,
                   add_response_value_t *response) {
  (void)context;
  int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->has_sum = true;
  response->sum = (int32_t)sum;
  return 0;
}

int main(void) {
  if (sample_network_init() < 0) { printk("network unavailable\n"); return 1; }
  udp_demo_endpoint_config_t config;
  wl_err_t error = udp_demo_endpoint_config_defaults(&config, wl_platform_environment());
  if (error != WL_OK) { printk("endpoint config: %d\n", error); return 1; }
  config.on_add = add;
  error = udp_demo_endpoint_init_config(&server, &config);
  if (error != WL_OK) { printk("endpoint init: %d\n", error); return 1; }
  error = sample_udp_open(&udp, udp_demo_endpoint_handle(&server));
  if (error != WL_OK) {
    printk("UDP open: %d\n", error);
    (void)udp_demo_endpoint_close(&server);
    return 1;
  }
  printk("UDP peer server ready on %s:%u\n", CONFIG_NET_CONFIG_MY_IPV4_ADDR,
           wl_zephyr_udp_local_port(&udp));
  uint32_t sequence = 0, due = k_uptime_get_32();
  for (;;) {
    error = udp_demo_endpoint_step(&server);
    if (error != WL_OK) break;
    uint32_t now = k_uptime_get_32();
    if ((int32_t)(now - due) >= 0) {
      telemetry_t sample;
      telemetry_clear(&sample);
      sample.has_sample = true;
      sample.sample = ++sequence;
      udp_demo_send_result_t sent = udp_demo_endpoint_send_telemetry(&server, &sample);
      if (sent.domain != UDP_DEMO_SEND_OK && sent.core_result != WL_ERR_BUSY) {
        error = WL_ERR_IO;
        break;
      }
      due = now + 20; /* Application sampling, not a second RPC timer. */
    }
    now = k_uptime_get_32();
    uint32_t wait = (int32_t)(due - now) > 0 ? due - now : 0;
    error = wl_zephyr_udp_wait(&udp, wait);
    if (error != WL_OK && error != WL_ERR_NO_DATA) break;
  }
  printk("UDP owner stopped: %d\n", error);
  int status = error == WL_ERR_CANCELLED ? 0 : 1;
  if (udp_demo_endpoint_close(&server) != WL_OK) status = 1;
  if (wl_zephyr_udp_close(&udp) != WL_OK) status = 1;
  return status;
}
