/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/ztest.h>

#include "v1_vectors.h"
#include "wirelink/cobs.h"

static size_t extract_raw(const wl_v1_conformance_vector_t *vector,
                          uint8_t *raw, size_t capacity) {
  size_t raw_len = 0U;

  switch (vector->envelope) {
  case WL_ENVELOPE_COBS_STREAM:
    zassert_true(vector->wire_len > 1U);
    zassert_equal(vector->wire[vector->wire_len - 1U], 0U);
    zassert_ok(wl_cobs_decode(vector->wire, vector->wire_len - 1U, raw,
                              capacity, &raw_len));
    break;
  case WL_ENVELOPE_BUS_LENGTH16:
    zassert_true(vector->wire_len >= 2U);
    raw_len = ((size_t)vector->wire[0] << 8U) | vector->wire[1];
    zassert_equal(raw_len, vector->wire_len - 2U);
    zassert_true(raw_len <= capacity);
    memcpy(raw, vector->wire + 2U, raw_len);
    break;
  case WL_ENVELOPE_NATIVE_PACKET:
    zassert_true(vector->wire_len <= capacity);
    raw_len = vector->wire_len;
    memcpy(raw, vector->wire, raw_len);
    break;
  default:
    zassert_unreachable("invalid envelope in conformance vector");
  }
  return raw_len;
}

ZTEST(wirelink_conformance, test_v1_encode_and_decode_vectors)
{
  uint8_t encoded[WL_FRAME_MAX_COBS_LEN];
  uint8_t raw[WL_FRAME_MAX_RAW_LEN];

  for (size_t index = 0U; index < wl_v1_conformance_vector_count; ++index) {
    const wl_v1_conformance_vector_t *vector =
        &wl_v1_conformance_vectors[index];
    wl_wire_packet_t packet = {
        .type = vector->type,
        .integrity = vector->integrity,
        .flags = vector->flags,
        .message_id = vector->message_id,
        .session_id = vector->session_id,
        .sequence = vector->sequence,
        .payload = vector->payload,
        .payload_len = vector->payload_len,
    };
    wl_frame_view_t decoded = {0};
    size_t encoded_len = 0U;
    size_t raw_len;

    zassert_ok(wl_frame_encode(&packet, vector->envelope, encoded,
                                sizeof(encoded), &encoded_len),
               "%s encode failed", vector->name);
    zassert_equal(encoded_len, vector->wire_len, "%s length changed",
                  vector->name);
    zassert_mem_equal(encoded, vector->wire, vector->wire_len,
                      "%s bytes changed", vector->name);

    raw_len = extract_raw(vector, raw, sizeof(raw));
    zassert_ok(wl_frame_decode(raw, raw_len, vector->integrity, &decoded),
               "%s decode failed", vector->name);
    zassert_equal(decoded.type, vector->type);
    zassert_equal(decoded.flags, vector->flags);
    zassert_equal(decoded.message_id, vector->message_id);
    zassert_equal(decoded.session_id, vector->session_id);
    zassert_equal(decoded.sequence, vector->sequence);
    zassert_equal(decoded.payload.length, vector->payload_len);
    if (vector->payload_len != 0U) {
      zassert_mem_equal(decoded.payload.data, vector->payload,
                        vector->payload_len);
    }
  }
}

ZTEST(wirelink_conformance, test_v1_rejection_vectors)
{
  uint8_t frame[sizeof(wl_v1_data_native_crc32c)];
  wl_frame_view_t decoded = {0};

  memcpy(frame, wl_v1_data_native_crc32c, sizeof(frame));
  frame[0] ^= 0x01U;
  zassert_equal(wl_frame_decode(frame, sizeof(frame), WL_INTEGRITY_CRC32C,
                                &decoded),
                WL_ERR_BAD_FRAME);

  memcpy(frame, wl_v1_data_native_crc32c, sizeof(frame));
  frame[2] = 2U;
  zassert_equal(wl_frame_decode(frame, sizeof(frame), WL_INTEGRITY_CRC32C,
                                &decoded),
                WL_ERR_PROTOCOL_VERSION);

  memcpy(frame, wl_v1_data_native_crc32c, sizeof(frame));
  frame[5] = 0x80U;
  zassert_equal(wl_frame_decode(frame, sizeof(frame), WL_INTEGRITY_CRC32C,
                                &decoded),
                WL_ERR_BAD_FRAME);

  memcpy(frame, wl_v1_data_native_crc32c, sizeof(frame));
  frame[21] = 6U;
  zassert_equal(wl_frame_decode(frame, sizeof(frame), WL_INTEGRITY_CRC32C,
                                &decoded),
                WL_ERR_BAD_FRAME);

  memcpy(frame, wl_v1_data_native_crc32c, sizeof(frame));
  frame[sizeof(frame) - 1U] ^= 0x01U;
  zassert_equal(wl_frame_decode(frame, sizeof(frame), WL_INTEGRITY_CRC32C,
                                &decoded),
                WL_ERR_CRC);

  zassert_equal(wl_frame_decode(wl_v1_data_native_crc32c,
                                sizeof(wl_v1_data_native_crc32c) - 1U,
                                WL_INTEGRITY_CRC32C, &decoded),
                WL_ERR_BAD_FRAME);
}

ZTEST_SUITE(wirelink_conformance, NULL, NULL, NULL, NULL, NULL);
