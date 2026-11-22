/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/udp.h>
#include <wirelink/node.h>

int main(void) {
  wl_udp_t *udp = NULL;
  wl_udp_config_t config = {.bind_address = "127.0.0.1"};
  if (wl_udp_open(&udp, &config, NULL) != WL_OK) return 1;
  wl_node_t node;
  uint8_t rx[2][32];
  wl_node_layout_t layout = {.rx_wire_max = 31, .rx_storage = &rx[0][0], .rx_storage_size = sizeof(rx)};
  int result = 1;
  if (wl_udp_local_port(udp) && wl_node_bind_transport(&node, &layout, wl_udp_transport(udp)) == WL_OK) {
    result = wl_node_step(&node, 0) == WL_OK && wl_node_close(&node, 0) == WL_OK ? 0 : 1;
  }
  wl_udp_close(udp);
  return result;
}
