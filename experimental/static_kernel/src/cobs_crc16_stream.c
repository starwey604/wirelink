/* SPDX-License-Identifier: Apache-2.0 */
#include "packet_stream_internal.h"
#include "wirelink/cobs.h"
#include "wirelink/crc.h"
size_t wl_packet_stream_cobs_crc16_bound(size_t size) {
  if (size > 2052U) return 0U;
  return wl_cobs_encoded_max_size(size + 2U) + 1U;
}
wl_err_t wl_packet_stream_cobs_crc16_init(wl_packet_stream_t *s, const wl_packet_stream_config_t *c) {
  return stream_init(s, c, c == NULL ? 0U : wl_packet_stream_cobs_crc16_bound(c->maximum_frame_size));
}
wl_packet_submit_t wl_packet_stream_cobs_crc16_submit(void *user, const uint8_t *data,
                                                     size_t length, wl_packet_token_t token) {
  wl_packet_stream_t *s = user;
  if (!valid_tx(s, data, length)) return WL_PACKET_FAILED;
  const uint16_t crc = wl_crc16_ccitt_false(data, length);
  uint8_t *out = s->config.tx_storage.data;
  size_t write = 1U, code_at = 0U; unsigned code = 1U;
  /* Append the CRC while encoding; no second raw frame/payload copy. The
   * configured worst-case bound was validated before admission. */
  for (size_t i = 0U; i < length + 2U; ++i) {
    uint8_t b = i < length ? data[i] : i == length ? (uint8_t)(crc >> 8U) : (uint8_t)crc;
    if (b == 0U) { out[code_at] = (uint8_t)code; code = 1U; code_at = write++; }
    else {
      out[write++] = b;
      if (++code == 255U) { out[code_at] = (uint8_t)code; code = 1U; code_at = write++; }
    }
  }
  out[code_at] = (uint8_t)code; out[write++] = 0U;
  return s->config.physical_submit(s->config.user, out, write, token);
}
wl_err_t wl_packet_stream_cobs_crc16_feed(wl_packet_stream_t *s, const uint8_t *bytes,
                                         size_t length, size_t *consumed) {
  if (consumed == NULL) return WL_ERR_INVALID_ARG;
  *consumed = 0U;
  if (s == NULL || (bytes == NULL && length != 0U)) return WL_ERR_INVALID_ARG;
  wl_err_t result = WL_OK;
  while (*consumed < length) {
    uint8_t b = bytes[*consumed];
    if (s->dropping) { ++*consumed; if (b == 0U) s->dropping = 0U; continue; }
    if (b == 0U && s->rx_length == 0U) { ++*consumed; continue; }
    wl_err_t e = claim(s);
    if (e != WL_OK) return result == WL_OK ? e : result;
    ++*consumed;
    if (b != 0U) {
      if (s->rx_length >= wl_packet_stream_cobs_crc16_bound(s->config.maximum_frame_size) - 1U) {
        (void)wl_packet_rx_abort(s->config.port, &s->claim);
        s->claimed = 0U; s->rx_length = 0U; s->dropping = 1U;
        if (result == WL_OK) result = WL_ERR_FRAME_TOO_LONG;
      } else s->claim.span.data[s->rx_length++] = b;
      continue;
    }
    size_t decoded = 0U;
    e = wl_cobs_decode_in_place(s->claim.span.data, s->rx_length, &decoded);
    if (e == WL_OK && decoded < 6U) e = WL_ERR_BAD_FRAME;
    if (e == WL_OK) {
      const uint8_t *p = s->claim.span.data;
      uint16_t crc = (uint16_t)((uint16_t)p[decoded - 2U] << 8U) | p[decoded - 1U];
      if (wl_crc16_ccitt_false(p, decoded - 2U) != crc) e = WL_ERR_CRC;
    }
    if (e == WL_OK) e = finish(s, decoded - 2U);
    else { (void)wl_packet_rx_abort(s->config.port, &s->claim); s->claimed = 0U; s->rx_length = 0U; }
    if (result == WL_OK) result = e;
  }
  return result;
}
