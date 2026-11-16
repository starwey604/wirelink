/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_port.h"
#include "packet_port_abi.h"

const size_t *cpp_test_port_layout(size_t *count) {
#define OFFSET(member) offsetof(wl_packet_port_t, member),
  static const size_t layout[] = {
      sizeof(wl_packet_port_t), _Alignof(wl_packet_port_t),
      WL_TEST_PORT_MEMBERS(OFFSET)
  };
#undef OFFSET
  *count = sizeof(layout) / sizeof(layout[0]);
  return layout;
}

static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t length,
                               wl_packet_token_t token) {
  (void)user; (void)data; (void)length; (void)token;
  return WL_PACKET_SENT;
}
wl_packet_port_t *cpp_test_port(uint32_t instance) {
  static wl_packet_port_t port;
  static uint8_t rx[2][2048];
  wl_packet_config_t config = {.instance = instance, .rx_storage = &rx[0][0],
      .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx), .submit = sink};
  return wl_packet_init(&port, &config) == WL_OK ? &port : NULL;
}
