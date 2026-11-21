/* SPDX-License-Identifier: Apache-2.0 */
#include "probe.h"
#include <string.h>
static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t length,
                               wl_packet_token_t token) {
  wl_packet_port_t *port = user;
  (void)token;
  wl_packet_claim_t claim;
  if (wl_packet_rx_claim(port, length, &claim) != WL_OK)
    return WL_PACKET_BUSY;
  memcpy(claim.span.data, data, length);
  return wl_packet_rx_commit(port, &claim, length) == WL_OK ? WL_PACKET_SENT : WL_PACKET_FAILED;
}
wl_err_t installed_test_init(probe_node_t *node, const probe_handlers_t *handlers) {
  const wl_packet_io_t io = {.submit = sink, .user = &node->port};
  return probe_init_io(node, 555U, &io, handlers, NULL);
}
