/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_port.h"
wl_packet_port_t *packet_udp_test_port(unsigned index) {
  static wl_packet_port_t ports[3];
  return index < 3 ? &ports[index] : NULL;
}
int packet_udp_test_init(unsigned index, wl_packet_sink_fn sink, wl_packet_notify_fn notify,
                         void *user) {
  static uint8_t bytes[3][2][256];
  static uint64_t instance = 880U;
  wl_packet_config_t c = {.instance = ++instance,
                          .rx_storage = &bytes[index][0][0],
                          .rx_unit_size = 256,
                          .rx_storage_size = sizeof(bytes[index]),
                          .submit = sink,
                          .notify = notify,
                          .user = user};
  return wl_packet_init(packet_udp_test_port(index), &c);
}
