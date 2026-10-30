/* SPDX-License-Identifier: Apache-2.0 */
#include "business.h"
#include "network.h"
#include <string.h>
static udp_demo_kernel_t client;
static sample_udp_t udp;
int main(void) {
  if (sample_network_init() < 0) { printk("network unavailable\n"); return 1; }
  wl_err_t error = sample_udp_open(&udp);
  if (error != WL_OK) { printk("UDP open: %d\n", error); return 1; }
  peer_client_t state = {0};
  const udp_demo_kernel_handlers_t handlers = {.on_rpc_add = peer_add, .on_message_telemetry = peer_sample};
  error = udp_demo_kernel_init(&client, &udp.port, &handlers, NULL, &state);
  if (error != WL_OK) { (void)wl_kernel_udp_close(&udp.adapter); return 1; }
  add_request_value_t request = {.has_left = true, .left = 20, .has_right = true, .right = 22};
  wl_kernel_call_handle_t call;
  error = udp_demo_kernel_add_start(&client, &request, k_uptime_get_32(), 1500, peer_completed, &state, &call);
  memset(&request, 0, sizeof(request));
  while (error == WL_OK && !state.completions) {
    error = peer_z_step(&udp.adapter, &client.core, k_uptime_get_32());
    if (error == WL_OK) error = peer_z_wait(&udp.adapter, &client.core, 100);
  }
  int status = 1;
  if (error == WL_OK && state.result.outcome == WL_KERNEL_SUCCESS) {
    printk("20 + 22 = %ld\n", (long)state.response.sum);
    uint32_t started = k_uptime_get_32();
    while (error == WL_OK && !state.samples && (uint32_t)(k_uptime_get_32() - started) < 500) {
      error = peer_z_step(&udp.adapter, &client.core, k_uptime_get_32());
      if (error == WL_OK) error = peer_z_wait(&udp.adapter, &client.core, 100);
    }
    if (state.samples && state.response.sum == 42) { printk("telemetry sample=%u\n", state.sample); status = 0; }
  } else printk("RPC: outcome=%d status=%d io=%d\n", state.result.outcome, state.result.status, error);
  if (peer_z_close(&udp.adapter, &client.core) != WL_OK) status = 1;
  return status;
}
