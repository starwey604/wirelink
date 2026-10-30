/* SPDX-License-Identifier: Apache-2.0 */
#include "business.h"
#include "network.h"
static udp_demo_kernel_t server;
static sample_udp_t udp;
int main(void) {
  if (sample_network_init() < 0) { printk("network unavailable\n"); return 1; }
  wl_err_t error = sample_udp_open(&udp);
  if (error != WL_OK) { printk("UDP open: %d\n", error); return 1; }
  const udp_demo_kernel_handlers_t handlers = {.on_rpc_add = peer_add};
  error = udp_demo_kernel_init(&server, &udp.port, &handlers, NULL, NULL);
  if (error != WL_OK) { (void)wl_kernel_udp_close(&udp.adapter); return 1; }
  printk("UDP peer server ready on %s:%u\n", CONFIG_NET_CONFIG_MY_IPV4_ADDR, wl_kernel_udp_local_port(&udp.adapter));
  uint32_t sequence = 0, due = k_uptime_get_32();
  for (;;) {
    uint32_t now = k_uptime_get_32();
    error = peer_z_step(&udp.adapter, &server.core, now);
    if (error != WL_OK) break;
    if ((int32_t)(now - due) >= 0) {
      telemetry_value_t sample = {.has_sample = true, .sample = ++sequence};
      error = udp_demo_kernel_send_telemetry(&server, &sample);
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
