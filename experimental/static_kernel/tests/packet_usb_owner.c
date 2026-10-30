/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_port.h"
wl_packet_port_t *packet_usb_test_storage(void) {
  static wl_packet_port_t port;
  return &port;
}
int packet_usb_test_init(uint64_t instance, wl_packet_sink_fn sink,
                         wl_packet_notify_fn notify, void *user) {
  static uint8_t rx[2][2048];
  wl_packet_config_t config = {.instance = instance, .rx_storage = &rx[0][0],
      .rx_unit_size = 2048, .rx_storage_size = sizeof(rx),
      .submit = sink, .notify = notify, .user = user};
  return wl_packet_init(packet_usb_test_storage(), &config);
}
