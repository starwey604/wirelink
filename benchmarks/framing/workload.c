/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include "wirelink/cobs.h"
#include "wirelink/crc.h"
#include <string.h>

const char *framing_mode_name(unsigned mode) {
  static const char *const names[] = {"spare", "exact", "overlap", "raw_decode", "cobs_decode"};
  return mode < FRAMING_MODES ? names[mode] : "invalid";
}

static void write_be(uint8_t *out, uint64_t value, size_t length) {
  for (size_t i = 0U; i < length; ++i)
    out[i] = (uint8_t)(value >> ((length - 1U - i) * 8U));
}

/* Independent raw construction + standalone CRC/COBS tools. */
static int reference(framing_fixture_t *f) {
  const wl_wire_packet_t *p = &f->packet;
  const unsigned kind = p->flags != 0U ? 1U : 0U;
  size_t raw_len = kind != 0U ? 16U : 4U;
  f->raw[0] = (uint8_t)(0xa4U | kind);
  f->raw[1] = 0U;
  write_be(f->raw + 2U, p->message_id, 2U);
  if (kind != 0U) {
    write_be(f->raw + 4U, p->session_id, 8U);
    write_be(f->raw + 12U, p->sequence, 4U);
  }
  memcpy(f->raw + raw_len, p->payload, p->payload_len);
  raw_len += p->payload_len;
  if (p->integrity != WL_INTEGRITY_NONE) {
    size_t crc_len = p->integrity == WL_INTEGRITY_CRC16 ? 2U : 4U;
    uint32_t crc = crc_len == 2U ? wl_crc16_ccitt_false(f->raw, raw_len) : wl_crc32c(f->raw, raw_len);
    write_be(f->raw + raw_len, crc, crc_len);
    raw_len += crc_len;
  }
  f->raw_len = raw_len;
  if (f->envelope == WL_ENVELOPE_COBS_STREAM) {
    int error = wl_cobs_encode(f->raw, raw_len, f->expected, sizeof(f->expected) - 1U, &f->exact_len);
    if (error != WL_OK) return error;
    f->expected[f->exact_len++] = 0U;
  } else if (f->envelope == WL_ENVELOPE_BUS_LENGTH16) {
    f->expected[0] = (uint8_t)(raw_len >> 8U);
    f->expected[1] = (uint8_t)raw_len;
    memcpy(f->expected + 2U, f->raw, raw_len);
    f->exact_len = raw_len + 2U;
  } else {
    memcpy(f->expected, f->raw, raw_len);
    f->exact_len = raw_len;
  }
  return WL_OK;
}

int framing_init(framing_fixture_t *f, unsigned mode, wl_envelope_type_t envelope,
                 wl_integrity_t integrity, unsigned pattern, size_t length) {
  if (f == NULL || mode >= FRAMING_MODES || pattern > 2U || length > WL_FRAME_MAX_PAYLOAD ||
      envelope < WL_ENVELOPE_COBS_STREAM || envelope > WL_ENVELOPE_BUS_LENGTH16 ||
      integrity < WL_INTEGRITY_NONE || integrity > WL_INTEGRITY_CRC32C ||
      ((mode == FRAMING_RAW_DECODE || mode == FRAMING_COBS_DECODE) && envelope != WL_ENVELOPE_COBS_STREAM))
    return WL_ERR_INVALID_ARG;
  memset(f, 0, sizeof(*f));
  f->mode = mode;
  f->envelope = envelope;
  for (size_t i = 0U; i < length; ++i)
    f->payload[i] = pattern == 0U || (pattern == 2U && i % 17U == 0U)
        ? 0U : (uint8_t)(i % 251U + 1U);
  f->packet = (wl_wire_packet_t){.type = WL_PACKET_DATA, .integrity = integrity,
      .flags = 0U,
      .message_id = 21U, .session_id = UINT64_C(0x123456789abcdef0),
      .payload = f->payload, .payload_len = length};
  return reference(f);
}

int framing_step(framing_fixture_t *f) {
  int error;
  if (f->mode == FRAMING_RAW_DECODE)
    return wl_frame_decode(f->raw, f->raw_len, f->packet.integrity, &f->rx_view);
  if (f->mode == FRAMING_COBS_DECODE) {
    size_t length;
    memcpy(f->tx_unit, f->expected, f->exact_len);
    error = wl_cobs_decode_in_place(f->tx_unit, f->exact_len - 1U, &length);
    return error != WL_OK ? error : wl_frame_decode(f->tx_unit, length, f->packet.integrity, &f->rx_view);
  }
  if (f->mode <= FRAMING_OVERLAP) {
    wl_wire_packet_t packet = f->packet;
    if (f->mode == FRAMING_OVERLAP) {
      memcpy(f->tx_unit, f->payload, f->packet.payload_len);
      packet.payload = f->tx_unit;
    }
    return wl_frame_encode(&packet, f->envelope, f->tx_unit,
        f->mode == FRAMING_EXACT ? f->exact_len : sizeof(f->tx_unit), &f->output_len);
  }
  return WL_ERR_INVALID_ARG;
}

int framing_check(framing_fixture_t *f) {
  /* Decoded views borrow fixture-owned storage until the next step. */
  if (f->mode >= FRAMING_RAW_DECODE) {
    const uint8_t *data = f->rx_view.payload.data;
    size_t length = f->rx_view.payload.length;
    return data != NULL && length == f->packet.payload_len && memcmp(data, f->payload, length) == 0
        ? WL_OK : WL_ERR_CORRUPT_PAYLOAD;
  }
  int error = reference(f);
  if (error != WL_OK) return error;
  const size_t length = f->output_len;
  const uint8_t *data = f->tx_unit;
  return data != NULL && length == f->exact_len && memcmp(data, f->expected, length) == 0
      ? WL_OK : WL_ERR_CORRUPT_PAYLOAD;
}
