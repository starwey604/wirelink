/* SPDX-License-Identifier: Apache-2.0 */
#include "frame_internal.h"

static uint16_t cobs_crc16_encode_span(wl_cobs_encode_state_t *state,
                                       const uint8_t *data, size_t length,
                                       uint16_t crc) {
  uint8_t *const out = state->out;
  size_t write_index = state->write_index;
  size_t code_index = state->code_index;
  uint8_t code = state->code;
  for (size_t i = 0U; i < length; ++i) {
    const uint8_t value = data[i];
    crc = wl_crc16_byte(crc, value);
    cobs_encode_byte(out, &write_index, &code_index, &code, value);
  }
  state->write_index = write_index;
  state->code_index = code_index;
  state->code = code;
  return crc;
}
int wl_frame_encode_cobs_crc16(const wl_config_t *config, const wl_wire_packet_t *packet,
                              uint8_t *out, size_t out_cap, size_t *out_len) {
  (void)config;
  uint8_t header[WL_FRAME_HEADER_SIZE];
  const size_t header_len = packet->type == WL_PACKET_ACK || packet->flags != 0U
      ? WL_FRAME_RELIABLE_HEADER_SIZE : WL_FRAME_BASE_HEADER_SIZE;
  const size_t raw_len = header_len + packet->payload_len + 2U;
  /* Packet bounds are already established. This is the standalone COBS
   * maximum formula on that bounded input, including the stream delimiter. */
  const size_t worst = raw_len + raw_len / 254U + 2U;
  *out_len = 0U;
  frame_write_header(packet, header_len, header);
  if (out_cap < worst || spans_overlap(packet->payload, packet->payload_len, out, worst - 1U)) {
    return wl_frame_encode_bounded(packet, WL_ENVELOPE_COBS_STREAM, WL_INTEGRITY_CRC16,
                                   header, header_len, raw_len, out, out_cap, out_len);
  }
  if (out == NULL) return WL_ERR_INVALID_ARG;
  wl_cobs_encode_state_t state = {
      .out = out, .write_index = 1U, .code_index = 0U, .code = 1U};
  uint16_t crc = cobs_crc16_encode_span(&state, header, header_len, 0xffffU);
  crc = cobs_crc16_encode_span(&state, packet->payload, packet->payload_len, crc);
  size_t write_index = state.write_index, code_index = state.code_index;
  uint8_t code = state.code;
  cobs_encode_byte(out, &write_index, &code_index, &code, (uint8_t)(crc >> 8U));
  cobs_encode_byte(out, &write_index, &code_index, &code, (uint8_t)crc);
  out[code_index] = code;
  out[write_index] = 0U;
  *out_len = write_index + 1U;
  return WL_OK;
}

int wl_frame_decode_cobs_crc16(const wl_config_t *config, uint8_t *encoded, size_t encoded_len,
                              wl_frame_view_t *out_view) {
  (void)config;
  size_t read = 0U, write = 0U;
  uint16_t crc = 0xffffU;
  if (encoded == NULL || out_view == NULL) return WL_ERR_INVALID_ARG;
  if (encoded_len == 0U) return WL_ERR_COBS_DECODE;
  while (read < encoded_len) {
    const uint8_t code = encoded[read++];
    const size_t count = (size_t)code - 1U;
    if (code == 0U || count > encoded_len - read) return WL_ERR_COBS_DECODE;
    for (size_t i = 0U; i < count; ++i) {
      const uint8_t value = encoded[read++];
      crc = wl_crc16_byte(crc, value);
      encoded[write++] = value;
    }
    if (code != 0xffU && read < encoded_len) {
      crc = wl_crc16_byte(crc, 0U);
      encoded[write++] = 0U;
    }
  }
  /* CCITT-FALSE with its big-endian state appended has residue zero. The
   * 16-bit trailer transform is bijective, so zero residue is equivalent to
   * comparing that trailer with CRC(header + payload). Check structure first
   * to retain the raw decoder's error precedence. */
  if (write == 0U) return WL_ERR_INVALID_ARG;
  return frame_parse(encoded, write, WL_INTEGRITY_CRC16, 2U, &crc, out_view);
}
