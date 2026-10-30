/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_stream.h"
void wl_packet_stream_notify(void *user) {
  wl_packet_stream_t *s = user;
  if (s->config.notify) s->config.notify(s->config.user);
}
wl_err_t wl_packet_stream_abort_rx(wl_packet_stream_t *s) {
  if (s == NULL) return WL_ERR_INVALID_ARG;
  wl_err_t result = s->claimed ? wl_packet_rx_abort(s->config.port, &s->claim) : WL_OK;
  s->claimed = 0U; s->rx_length = 0U; s->expected = 0U;
  s->prefix_bytes = 0U; s->discard = 0U; s->dropping = 0U;
  return result;
}
