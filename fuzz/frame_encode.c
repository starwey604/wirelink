/* SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include <string.h>
#include "wirelink/cobs.h"
#include "wirelink/frame.h"

static void write_be(uint8_t *out, uint64_t value, size_t length) {
  for (size_t i = 0U; i < length; ++i) {
    out[length - 1U - i] = (uint8_t)(value >> (8U * i));
  }
}

/* Independent raw-byte oracle: a fixed library cannot encode native packets
 * merely to supply a reference for its supported COBS output. */
static size_t reference_raw(const wl_wire_packet_t *packet, uint8_t *raw) {
  const uint8_t kind = packet->type == WL_PACKET_ACK ? 2U :
      packet->flags != 0U ? 1U : 0U;
  size_t length = kind != 0U ? 16U : 4U;
  raw[0] = (uint8_t)(WL_FRAME_PREFIX | kind);
  raw[1] = 0U;
  write_be(raw + 2U, packet->message_id, 2U);
  if (kind != 0U) {
    write_be(raw + 4U, packet->session_id, 8U);
    write_be(raw + 12U, packet->sequence, 4U);
  }
  memcpy(raw + length, packet->payload, packet->payload_len);
  length += packet->payload_len;
  if (packet->integrity == WL_INTEGRITY_CRC16) {
    uint16_t crc = UINT16_C(0xffff);
    for (size_t i = 0U; i < length; ++i) {
      crc ^= (uint16_t)((uint16_t)raw[i] << 8U);
      for (unsigned bit = 0U; bit < 8U; ++bit) {
        crc = (uint16_t)((crc << 1U) ^ ((crc & UINT16_C(0x8000)) != 0U
            ? UINT16_C(0x1021) : 0U));
      }
    }
    write_be(raw + length, crc, 2U);
    length += 2U;
  } else if (packet->integrity == WL_INTEGRITY_CRC32C) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0U; i < length; ++i) {
      crc ^= raw[i];
      for (unsigned bit = 0U; bit < 8U; ++bit) {
        crc = (crc >> 1U) ^ ((crc & 1U) != 0U
            ? UINT32_C(0x82f63b78) : 0U);
      }
    }
    write_be(raw + length, crc ^ UINT32_MAX, 4U);
    length += 4U;
  }
  return length;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  uint8_t raw[WL_FRAME_MAX_RAW_LEN], expected[WL_FRAME_MAX_COBS_LEN];
  uint8_t arena[2U * WL_FRAME_MAX_COBS_LEN + 256U], saved[sizeof(arena)];
  size_t raw_len, expected_len, actual_len = SIZE_MAX;
  if (size < 4U || size - 4U > WL_FRAME_MAX_PAYLOAD) return 0;
  const wl_envelope_type_t envelope = (wl_envelope_type_t)(data[3] % 3U);
  const size_t output_offset = 128U + (data[0] / 3U) % 8U;
  wl_wire_packet_t packet = {
      .type = data[0] % 3U == 2U ? WL_PACKET_ACK : WL_PACKET_DATA,
      .integrity = (wl_integrity_t)(data[1] % 3U),
      .flags = data[0] % 3U == 1U ? WL_PACKET_FLAG_RELIABLE : 0U,
      .message_id = data[0] % 3U == 2U ? 0U : 21U,
      .session_id = UINT64_C(0x123456789abcdef0), .sequence = data[3],
      .payload = data + 4U, .payload_len = data[0] % 3U == 2U ? 0U : size - 4U};
  if (wl_frame_encode_overhead(envelope, packet.integrity) == 0U) {
    memset(arena, 0xA5, sizeof(arena));
    memcpy(saved, arena, sizeof(arena));
    if (wl_frame_encode(&packet, envelope, arena, sizeof(arena), &actual_len) !=
            WL_ERR_NOT_SUPPORTED || actual_len != 0U ||
        memcmp(arena, saved, sizeof(arena)) != 0) abort();
    return 0;
  }
  raw_len = reference_raw(&packet, raw);
  if (envelope == WL_ENVELOPE_COBS_STREAM) {
    if (wl_cobs_encode(raw, raw_len, expected, sizeof(expected) - 1U, &expected_len) != WL_OK) abort();
    expected[expected_len++] = 0U;
  } else if (envelope == WL_ENVELOPE_BUS_LENGTH16) {
    expected[0] = (uint8_t)(raw_len >> 8U);
    expected[1] = (uint8_t)raw_len;
    memcpy(expected + 2U, raw, raw_len);
    expected_len = raw_len + 2U;
  } else {
    memcpy(expected, raw, raw_len);
    expected_len = raw_len;
  }
  const size_t worst_len = envelope == WL_ENVELOPE_COBS_STREAM
      ? wl_cobs_encoded_max_size(raw_len) + 1U : expected_len;
  const size_t offsets[] = {0U, 16U, 64U, 128U, 192U, 2300U,
      output_offset + expected_len - 1U, output_offset + expected_len,
      output_offset + worst_len - 1U, output_offset + worst_len};
  const size_t capacity = data[2] % 3U == 0U ? WL_FRAME_MAX_COBS_LEN :
      data[2] % 3U == 1U ? expected_len : ((size_t)data[3] * 17U) % expected_len;
  const size_t offset = offsets[(data[2] / 3U) % (sizeof(offsets) / sizeof(offsets[0]))];
  memset(arena, 0xA5, sizeof(arena));
  memcpy(arena + offset, packet.payload, packet.payload_len);
  memcpy(saved, arena, sizeof(arena));
  packet.payload = arena + offset;
  int error = wl_frame_encode(&packet, envelope, arena + output_offset, capacity, &actual_len);
  if (capacity < expected_len) {
    if (error != WL_ERR_BUF_TOO_SMALL || actual_len != 0U || memcmp(arena, saved, sizeof(arena)) != 0) abort();
  } else {
    if (error != WL_OK || actual_len != expected_len ||
        memcmp(arena + output_offset, expected, expected_len) != 0 ||
        memcmp(arena, saved, output_offset) != 0 ||
        memcmp(arena + output_offset + actual_len, saved + output_offset + actual_len,
            sizeof(arena) - output_offset - actual_len) != 0) abort();
  }
  return 0;
}
