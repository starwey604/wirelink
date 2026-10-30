/* SPDX-License-Identifier: Apache-2.0 */
#include "packet_stream_internal.h"
size_t wl_packet_stream_length16_none_bound(size_t size) {
  return size <= 2052U ? size + 2U : 0U;
}
wl_err_t wl_packet_stream_length16_none_init(wl_packet_stream_t *s, const wl_packet_stream_config_t *c) {
  return stream_init(s, c, c == NULL ? 0U : wl_packet_stream_length16_none_bound(c->maximum_frame_size));
}
wl_packet_submit_t wl_packet_stream_length16_none_submit(void *user, const uint8_t *data,
                                                        size_t length, wl_packet_token_t token) {
  wl_packet_stream_t *s = user;
  if (!valid_tx(s, data, length)) return WL_PACKET_FAILED;
  uint8_t *out = s->config.tx_storage.data;
  out[0] = (uint8_t)(length >> 8U); out[1] = (uint8_t)length;
  memcpy(out + 2U, data, length);
  return s->config.physical_submit(s->config.user, out, length + 2U, token);
}
wl_err_t wl_packet_stream_length16_none_feed(wl_packet_stream_t *s, const uint8_t *bytes,
                                            size_t length, size_t *consumed) {
  if (consumed == NULL) return WL_ERR_INVALID_ARG;
  *consumed = 0U;
  if (s == NULL || (bytes == NULL && length != 0U)) return WL_ERR_INVALID_ARG;
  wl_err_t result = WL_OK;
  while (*consumed < length) {
    if (s->discard) { --s->discard; ++*consumed; continue; }
    if (s->prefix_bytes < 2U) {
      s->expected = (s->expected << 8U) | bytes[(*consumed)++];
      if (++s->prefix_bytes < 2U) continue;
      if (s->expected < 4U || s->expected > s->config.maximum_frame_size) {
        s->discard = s->expected; s->expected = 0U; s->prefix_bytes = 0U;
        if (result == WL_OK) result = WL_ERR_BAD_FRAME;
      }
      continue;
    }
    wl_err_t e = claim(s);
    if (e != WL_OK) return result == WL_OK ? e : result;
    size_t n = s->expected - s->rx_length;
    if (n > length - *consumed) n = length - *consumed;
    memcpy(s->claim.span.data + s->rx_length, bytes + *consumed, n);
    s->rx_length += n; *consumed += n;
    if (s->rx_length == s->expected) {
      e = finish(s, s->expected); s->expected = 0U; s->prefix_bytes = 0U;
      if (result == WL_OK) result = e;
    }
  }
  return result;
}
