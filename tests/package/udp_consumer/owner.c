/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/packet_port.h>
wl_packet_port_t *installed_udp_port(void) { static wl_packet_port_t port; return &port; }
int installed_udp_prepare(wl_packet_sink_fn submit, wl_packet_notify_fn notify, void *user) {
  static uint8_t rx[2][256];
  wl_packet_config_t config = {.instance = 91, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx),
    .submit = submit, .notify = notify, .user = user};
  return wl_packet_init(installed_udp_port(), &config);
}
