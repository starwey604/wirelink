/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/frame.h"

ZTEST(wirelink_frame_unit, test_encode_decode_roundtrip_crc32c)
{
  const uint8_t payload[] = {0x10, 0x20, 0x00, 0x30, 0x40, 0x50};
  uint8_t encoded[WL_FRAME_MAX_RAW_LEN] = {0};
  size_t encoded_len = 0;
  wl_frame_view_t view = {0};

  wl_wire_packet_t packet = {0};
  packet.type = WL_PACKET_DATA;
  packet.flags = 0;
  packet.cmd_id = 1U;
  packet.session_id = 0x0102030405060708ULL;
  packet.sequence = 42U;
  packet.integrity = WL_INTEGRITY_CRC32C;
  packet.payload = payload;
  packet.payload_len = sizeof(payload);

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                            sizeof(encoded), &encoded_len));
  zassert_equal(packet.payload_len + WL_FRAME_HEADER_SIZE + 4U, encoded_len);

  zassert_ok(wl_frame_decode(encoded, encoded_len, WL_INTEGRITY_CRC32C, &view));
  zassert_equal(view.type, WL_PACKET_DATA);
  zassert_equal(view.flags, packet.flags);
  zassert_equal(view.cmd_id, packet.cmd_id);
  zassert_equal(view.session_id, packet.session_id);
  zassert_equal(view.sequence, packet.sequence);
  zassert_mem_equal(view.payload.data, payload, sizeof(payload));
}

ZTEST(wirelink_frame_unit, test_reject_invalid_session_and_flags)
{
  const uint8_t payload[] = {0x01, 0x02, 0x03};
  uint8_t encoded[WL_FRAME_MAX_RAW_LEN] = {0};
  size_t encoded_len = 0;
  wl_frame_view_t view = {0};

  wl_wire_packet_t packet = {0};
  packet.type = WL_PACKET_DATA;
  packet.flags = 0;
  packet.cmd_id = 1U;
  packet.session_id = 1ULL;
  packet.sequence = 1U;
  packet.integrity = WL_INTEGRITY_NONE;
  packet.payload = payload;
  packet.payload_len = sizeof(payload);

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                            sizeof(encoded), &encoded_len));

  memset(encoded + 6U, 0, sizeof(uint64_t));
  zassert_equal(wl_frame_decode(encoded, encoded_len, WL_INTEGRITY_NONE, &view),
                WL_ERR_BAD_FRAME);

  encoded[5] = 0x80;
  zassert_equal(wl_frame_decode(encoded, encoded_len, WL_INTEGRITY_NONE, &view),
                WL_ERR_BAD_FRAME);
}

ZTEST(wirelink_frame_unit, test_encode_rejects_zero_session)
{
  wl_wire_packet_t packet = {0};
  uint8_t encoded[WL_FRAME_MAX_RAW_LEN] = {0};
  size_t encoded_len = 0;

  packet.type = WL_PACKET_DATA;
  packet.flags = 0U;
  packet.cmd_id = 1U;
  packet.session_id = 0ULL;
  packet.sequence = 1U;
  packet.integrity = WL_INTEGRITY_NONE;
  packet.payload = NULL;
  packet.payload_len = 0U;

  zassert_equal(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                               sizeof(encoded), &encoded_len),
                WL_ERR_INVALID_ARG);
}

ZTEST(wirelink_frame_unit, test_nack_packet_is_rejected)
{
  uint8_t encoded[WL_FRAME_MAX_RAW_LEN] = {0};
  size_t encoded_len = 0;
  wl_frame_view_t view = {0};

  wl_wire_packet_t packet = {0};
  packet.type = WL_PACKET_DATA;
  packet.flags = 0U;
  packet.cmd_id = 1U;
  packet.session_id = 0x0102030405060708ULL;
  packet.sequence = 1U;
  packet.integrity = WL_INTEGRITY_NONE;
  packet.payload = NULL;
  packet.payload_len = 0U;

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                            sizeof(encoded), &encoded_len));

  encoded[4] = WL_PACKET_NACK;
  zassert_equal(wl_frame_decode(encoded, encoded_len, WL_INTEGRITY_NONE, &view),
                WL_ERR_BAD_FRAME);
}

ZTEST(wirelink_frame_unit, test_crc16_tamper_detection)
{
  const uint8_t payload[] = {0xAA, 0xBB, 0xCC};
  uint8_t encoded[WL_FRAME_HEADER_SIZE + sizeof(payload) + 2U] = {0};
  size_t encoded_len = 0;
  wl_frame_view_t view = {0};

  wl_wire_packet_t packet = {0};
  packet.type = WL_PACKET_DATA;
  packet.flags = 0;
  packet.cmd_id = 7U;
  packet.session_id = 0x0102030405060708ULL;
  packet.sequence = 3U;
  packet.integrity = WL_INTEGRITY_CRC16;
  packet.payload = payload;
  packet.payload_len = sizeof(payload);

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                            sizeof(encoded), &encoded_len));

  encoded[WL_FRAME_HEADER_SIZE + 1U] ^= 0xFFU;
  zassert_equal(wl_frame_decode(encoded, encoded_len, WL_INTEGRITY_CRC16, &view),
                WL_ERR_CRC);
}

ZTEST_SUITE(wirelink_frame_unit, NULL, NULL, NULL, NULL, NULL);
