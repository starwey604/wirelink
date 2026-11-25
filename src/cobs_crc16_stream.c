/* SPDX-License-Identifier: Apache-2.0 */
#include "packet_stream_internal.h"
#include "wirelink/cobs.h"
#include "wirelink/crc.h"
size_t wl_packet_stream_cobs_crc16_bound(size_t size) {
  if (size > 2052U)
    return 0U;
  return wl_cobs_encoded_max_size(size + 2U) + 1U;
}
wl_err_t wl_packet_stream_cobs_crc16_init(wl_packet_stream_t *s,
                                          const wl_packet_stream_config_t *c) {
  if (c == NULL || c->port == NULL)
    return WL_ERR_INVALID_ARG;
  wl_packet_stream_config_t selected = *c;
  if (selected.maximum_rx_frame_size == 0U) {
    size_t unit = wl_packet_rx_unit_size(c->port);
    size_t m = unit > 2U ? unit - 2U : 0U;
    size_t n = m - (m + 1U) / 255U;
    size_t limit = n > 2U ? n - 2U : 0U;
    if (limit > 2052U)
      limit = 2052U;
    selected.maximum_rx_frame_size = limit;
  }
  return stream_init(s, &selected, wl_packet_stream_cobs_crc16_bound(c->maximum_frame_size),
                     wl_packet_stream_cobs_crc16_bound(selected.maximum_rx_frame_size));
}
wl_packet_submit_t wl_packet_stream_cobs_crc16_submit(void *user, const uint8_t *data,
                                                      size_t length, wl_packet_token_t token) {
  wl_packet_stream_t *s = user;
  if (!valid_tx(s, data, length))
    return WL_PACKET_FAILED;
  const uint16_t crc = wl_crc16_ccitt_false(data, length);
  uint8_t *out = s->config.tx_storage.data;
  size_t write = 1U, code_at = 0U;
  unsigned code = 1U;
  /* Append the CRC while encoding; no second raw frame/payload copy. The
   * configured worst-case bound was validated before admission. */
  const uint8_t tail[] = {(uint8_t)(crc >> 8U), (uint8_t)crc};
  const uint8_t *part = data;
  size_t count = length;
  for (unsigned run = 0U; run < 2U; ++run) {
    for (size_t i = 0U; i < count; ++i) {
      uint8_t b = part[i];
      if (b != 0U) {
        out[write++] = b;
        if (++code != 255U)
          continue;
      }
      out[code_at] = (uint8_t)code;
      code = 1U;
      code_at = write++;
    }
    part = tail;
    count = sizeof(tail);
  }
  out[code_at] = (uint8_t)code;
  out[write++] = 0U;
  return s->config.physical_submit(s->config.user, out, write, token);
}
static wl_err_t decode_body(uint8_t *data, size_t length, size_t *canonical) {
  *canonical = 0U;
  size_t decoded = 0U;
  wl_err_t error = wl_cobs_decode_in_place(data, length, &decoded);
  if (error != WL_OK)
    return error;
  if (decoded < 6U)
    return WL_ERR_BAD_FRAME;
  if (decoded > 2054U)
    return WL_ERR_FRAME_TOO_LONG;
  uint16_t crc = (uint16_t)((uint16_t)data[decoded - 2U] << 8U) | data[decoded - 1U];
  if (wl_crc16_ccitt_false(data, decoded - 2U) != crc)
    return WL_ERR_CRC;
  *canonical = decoded - 2U;
  return WL_OK;
}
wl_err_t wl_packet_cobs_crc16_decode(uint8_t *data, size_t length, size_t *canonical) {
  if (canonical == NULL)
    return WL_ERR_INVALID_ARG;
  *canonical = 0U;
  if (data == NULL || length == 0U || data[length - 1U] != 0U)
    return WL_ERR_BAD_FRAME;
  if (length > wl_packet_stream_cobs_crc16_bound(2052U))
    return WL_ERR_FRAME_TOO_LONG;
  return decode_body(data, length - 1U, canonical);
}
wl_err_t wl_packet_stream_cobs_crc16_feed(wl_packet_stream_t *s, const uint8_t *bytes,
                                          size_t length, size_t *consumed) {
  if (consumed == NULL)
    return WL_ERR_INVALID_ARG;
  *consumed = 0U;
  if (s == NULL || (bytes == NULL && length != 0U))
    return WL_ERR_INVALID_ARG;
  wl_err_t result = WL_OK;
  size_t pos = 0U;
  while (pos < length) {
    if (s->dropping) {
      const uint8_t *end = memchr(bytes + pos, 0, length - pos);
      if (end == NULL) {
        pos = length;
        break;
      }
      pos = (size_t)(end - bytes) + 1U;
      s->dropping = 0U;
      continue;
    }
    if (s->rx_length == 0U) {
      while (pos < length && bytes[pos] == 0U)
        ++pos;
      if (pos == length)
        break;
    }
    wl_err_t e = claim(s);
    if (e != WL_OK) {
      if (result == WL_OK)
        result = e;
      break;
    }
    const uint8_t *start = bytes + pos;
    const uint8_t *end = memchr(start, 0, length - pos);
    size_t chunk = end != NULL ? (size_t)(end - start) : length - pos;
    pos += chunk + (end != NULL);
    if (chunk > (size_t)s->max_rx_wire_size - 1U - s->rx_length) {
      (void)wl_packet_rx_abort(s->config.port, &s->claim);
      s->claimed = 0U;
      s->rx_length = 0U;
      s->dropping = (end == NULL);
      if (result == WL_OK)
        result = WL_ERR_FRAME_TOO_LONG;
      continue;
    }
    memcpy(s->claim.span.data + s->rx_length, start, chunk);
    s->rx_length += chunk;
    if (end == NULL)
      break;
    size_t decoded = 0U;
    e = decode_body(s->claim.span.data, s->rx_length, &decoded);
    if (e == WL_OK)
      e = finish(s, decoded);
    else {
      (void)wl_packet_rx_abort(s->config.port, &s->claim);
      s->claimed = 0U;
      s->rx_length = 0U;
    }
    if (result == WL_OK)
      result = e;
  }
  *consumed = pos;
  return result;
}
