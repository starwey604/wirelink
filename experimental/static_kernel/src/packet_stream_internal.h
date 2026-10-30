/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WL_PACKET_STREAM_INTERNAL_H
#define WL_PACKET_STREAM_INTERNAL_H
#include "wirelink/packet_stream.h"
#include "wirelink/kernel_wire.h"
#include <string.h>
static inline wl_err_t stream_init(wl_packet_stream_t *s, const wl_packet_stream_config_t *c,
                                   size_t bound, size_t rx_bound) {
  if (s == NULL || c == NULL || c->port == NULL || c->physical_submit == NULL ||
      c->maximum_frame_size < 4U || c->maximum_rx_frame_size < 4U || bound == 0U ||
      rx_bound == 0U || c->tx_storage.data == NULL || c->tx_storage.length < bound ||
      wl_packet_rx_unit_size(c->port) < rx_bound)
    return WL_ERR_INVALID_ARG;
  const wl_packet_stream_config_t saved = *c;
  memset(s, 0, sizeof(*s));
  s->config = saved;
  return WL_OK;
}
static inline int valid_tx(wl_packet_stream_t *s, const uint8_t *data, size_t length) {
  wl_kernel_frame_t frame;
  return s != NULL && length <= s->config.maximum_frame_size &&
         wl_kernel_native_none_read(data, length, &frame) == WL_OK;
}
static inline wl_err_t claim(wl_packet_stream_t *s) {
  if (s->claimed)
    return WL_OK;
  wl_err_t e =
      wl_packet_rx_claim(s->config.port, wl_packet_rx_unit_size(s->config.port), &s->claim);
  if (e == WL_OK)
    s->claimed = 1U;
  return e;
}
static inline wl_err_t finish(wl_packet_stream_t *s, size_t length) {
  wl_kernel_frame_t frame;
  wl_err_t e = wl_kernel_native_none_read(s->claim.span.data, length, &frame);
  if (e == WL_OK && length > s->config.maximum_rx_frame_size)
    e = WL_ERR_FRAME_TOO_LONG;
  if (e == WL_OK)
    e = wl_packet_rx_commit(s->config.port, &s->claim, length);
  if (e != WL_OK)
    (void)wl_packet_rx_abort(s->config.port, &s->claim);
  s->claimed = 0U;
  s->rx_length = 0U;
  return e;
}
#endif
