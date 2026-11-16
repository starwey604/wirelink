/* SPDX-License-Identifier: Apache-2.0 */
#include "business.h"
#include "network.h"
static udp_server_node_t server;
static sample_udp_t udp;
int32_t udp_server_on_add(udp_server_node_t *self, const add_request_t *request, add_response_t *response) {
  return peer_add(self->user, request, response);
}
int main(void) {
  if (sample_network_init() < 0) { printk("network unavailable\n"); return 1; }
  wl_err_t error = sample_udp_open(&udp);
  if (error != WL_OK) { printk("UDP open: %d\n", error); return 1; }
  error = udp_server_init(&server, &udp.port);
  if (error != WL_OK) { (void)wl_kernel_udp_close(&udp.adapter); return 1; }
  printk("UDP peer server ready on %s:%u\n", CONFIG_NET_CONFIG_MY_IPV4_ADDR, wl_kernel_udp_local_port(&udp.adapter));
  uint32_t sequence = 0, due = k_uptime_get_32();
  for (;;) {
    uint32_t now = k_uptime_get_32();
    error = peer_z_step(&udp.adapter, &server.core, now);
    if (error != WL_OK) break;
    if ((int32_t)(now - due) >= 0) {
      telemetry_t sample = {.sample = ++sequence};
      error = udp_server_send_telemetry(&server, &sample);
      if (error != WL_OK && error != WL_ERR_WOULD_BLOCK) break;
      due = now + 20;
    }
    now = k_uptime_get_32();
    error = peer_z_wait(&udp.adapter, &server.core, (int32_t)(due - now) > 0 ? due - now : 0);
    if (error != WL_OK) break;
  }
  printk("UDP owner stopped: %d\n", error);
  int status = peer_z_close(&udp.adapter, &server.core) == WL_OK ? 0 : 1;
  return status;
}
