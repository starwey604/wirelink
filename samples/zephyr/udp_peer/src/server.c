/* SPDX-License-Identifier: Apache-2.0 */
#include "business.h"
#include "network.h"
static udp_server_node_t server;
static sample_udp_t udp;
int32_t udp_server_on_add(udp_server_node_t *self, const add_request_t *request, add_response_t *response) {
  return peer_add(self->node.user, request, response);
}
int main(void) {
  if (sample_network_init() < 0) { printk("network unavailable\n"); return 1; }
  uint32_t instance;
  wl_err_t error = wl_session_next(wl_platform_environment().session, 0U, &instance);
  if (error != WL_OK) return 1;
  wl_zephyr_udp_config_t config;
  error = sample_udp_configure(&config);
  if (error == WL_OK)
    error = udp_server_bind_instance(&server, instance, wl_zephyr_udp_transport(&udp.adapter, &config));
  if (error != WL_OK) { return 1; }
  printk("UDP peer server ready on %s:%u\n", CONFIG_NET_CONFIG_MY_IPV4_ADDR, wl_zephyr_udp_local_port(&udp.adapter));
  uint32_t sequence = 0, due = k_uptime_get_32();
  for (;;) {
    uint32_t now = k_uptime_get_32();
    error = wl_node_step(&server.node, now);
    if (error != WL_OK) break;
    if ((int32_t)(now - due) >= 0) {
      telemetry_t sample = {.sample = ++sequence};
      error = udp_server_send_telemetry(&server, &sample);
      if (error != WL_OK && error != WL_ERR_WOULD_BLOCK) break;
      due = now + 20;
    }
    now = k_uptime_get_32();
    error = wl_node_wait(&server.node, k_uptime_get_32(), (int32_t)(due - now) > 0 ? due - now : 0);
    if (error != WL_OK) break;
  }
  printk("UDP owner stopped: %d\n", error);
  int status = wl_node_close(&server.node, k_uptime_get_32()) == WL_OK ? 0 : 1;
  return status;
}
