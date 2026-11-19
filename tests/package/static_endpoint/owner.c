/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_port.h"
#include <string.h>
static wl_packet_port_t port;
static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t length,
                               wl_packet_token_t token) {
  (void)user;
  (void)token;
  wl_packet_claim_t claim;
  if (wl_packet_rx_claim(&port, 256, &claim) != WL_OK)
    return WL_PACKET_BUSY;
  memcpy(claim.span.data, data, length);
  return wl_packet_rx_commit(&port, &claim, length) == WL_OK ? WL_PACKET_SENT : WL_PACKET_FAILED;
}
wl_packet_port_t *installed_test_port(void) {
  static uint8_t rx[2][256];
  wl_packet_config_t c = {.instance = 555,
                          .rx_storage = &rx[0][0],
                          .rx_unit_size = 256,
                          .rx_storage_size = sizeof(rx),
                          .submit = sink};
  return wl_packet_init(&port, &c) == WL_OK ? &port : NULL;
}
