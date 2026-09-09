/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/cobs.h"
#include "wirelink/frame.h"

static uint8_t boundary_payload[WL_FRAME_MAX_PAYLOAD];
static uint8_t boundary_native[WL_FRAME_MAX_RAW_LEN];
static uint8_t boundary_length16[WL_FRAME_MAX_RAW_LEN + 2U];
static uint8_t boundary_cobs[WL_FRAME_MAX_COBS_LEN];
static uint8_t boundary_decoded[WL_FRAME_MAX_RAW_LEN];
static uint8_t overlap_payload[600U];
static uint8_t overlap_expected[WL_FRAME_MAX_COBS_LEN];
static uint8_t overlap_arena[WL_FRAME_MAX_COBS_LEN * 2U + 128U];

ZTEST(wirelink_frame_unit, test_overlap_at_exact_and_worst_case_output_end)
{
  static uint8_t saved[sizeof(overlap_arena)];
  /* The conservative overlap test may classify payload beyond the exact
   * output boundary as aliased. Staging must only modify actual output. */
  for (unsigned reliable = 0U; reliable < 2U; ++reliable) {
    for (int integrity = WL_INTEGRITY_NONE; integrity <= WL_INTEGRITY_CRC32C; ++integrity) {
      for (unsigned pattern = 0U; pattern < 2U; ++pattern) {
        for (size_t i = 0U; i < sizeof(boundary_payload); ++i)
          boundary_payload[i] = pattern == 0U || i % 17U == 0U ? 0U : (uint8_t)(i % 251U + 1U);
        wl_wire_packet_t packet = {.type = WL_PACKET_DATA, .integrity = integrity,
            .flags = reliable ? WL_PACKET_FLAG_RELIABLE : 0U, .message_id = 21U,
            .session_id = 1U, .sequence = 255U, .payload = boundary_payload,
            .payload_len = sizeof(boundary_payload)};
        size_t raw_len, expected_len, actual_len;
        zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET,
            boundary_native, sizeof(boundary_native), &raw_len));
        zassert_ok(wl_cobs_encode(boundary_native, raw_len,
            overlap_expected, sizeof(overlap_expected) - 1U, &expected_len));
        overlap_expected[expected_len++] = 0U;
        const size_t worst_len = wl_cobs_encoded_max_size(raw_len) + 1U;
        zassert_true(expected_len < worst_len);
        const size_t offsets[] = {expected_len - 1U, expected_len,
            worst_len - 1U, worst_len};
        const size_t capacities[] = {expected_len - 1U, expected_len, worst_len};
        for (size_t layout = 0U; layout < ARRAY_SIZE(offsets); ++layout) {
          for (size_t capacity = 0U; capacity < ARRAY_SIZE(capacities); ++capacity) {
            uint8_t *output = overlap_arena + 64U;
            memset(overlap_arena, 0xA5, sizeof(overlap_arena));
            packet.payload = output + offsets[layout];
            memcpy((uint8_t *)packet.payload, boundary_payload, packet.payload_len);
            memcpy(saved, overlap_arena, sizeof(saved));
            int error = wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM,
                output, capacities[capacity], &actual_len);
            if (capacity == 0U) {
              zassert_equal(error, WL_ERR_BUF_TOO_SMALL);
              zassert_equal(actual_len, 0U);
              zassert_mem_equal(overlap_arena, saved, sizeof(saved));
            } else {
              zassert_ok(error);
              zassert_equal(actual_len, expected_len);
              zassert_mem_equal(output, overlap_expected, expected_len);
              zassert_mem_equal(overlap_arena, saved, 64U);
              zassert_mem_equal(output + expected_len, saved + 64U + expected_len,
                  sizeof(saved) - 64U - expected_len);
            }
          }
        }
      }
    }
  }
}

ZTEST(wirelink_frame_unit, test_cobs_spare_exact_overlap_and_short_match_oracle)
{
  static const size_t lengths[] = {0U, 1U, 32U, 120U, 231U, 232U, 233U,
      237U, 238U, 239U, 249U, 250U, 251U, 253U, 254U, 255U, 507U, 508U,
      509U, 1024U, WL_FRAME_MAX_PAYLOAD};
  for (unsigned pattern = 0U; pattern < 3U; ++pattern) {
    for (size_t i = 0U; i < sizeof(boundary_payload); ++i)
      boundary_payload[i] = pattern == 0U || (pattern == 2U && i % 17U == 0U)
          ? 0U : (uint8_t)(i % 251U + 1U);
    for (int integrity = WL_INTEGRITY_NONE; integrity <= WL_INTEGRITY_CRC32C; ++integrity) {
      for (unsigned kind = 0U; kind < 3U; ++kind) {
        for (size_t index = 0U; index < (kind == 2U ? 1U : ARRAY_SIZE(lengths)); ++index) {
          wl_wire_packet_t packet = {.type = kind == 2U ? WL_PACKET_ACK : WL_PACKET_DATA,
              .integrity = integrity, .flags = kind == 1U ? WL_PACKET_FLAG_RELIABLE : 0U,
              .message_id = kind == 2U ? 0U : 21U, .session_id = UINT64_C(0x123456789abcdef0),
              .sequence = 0xabcdef01U, .payload = boundary_payload, .payload_len = lengths[index]};
          size_t raw_len, expected_len, actual_len;
          zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET,
              boundary_native, sizeof(boundary_native), &raw_len));
          zassert_ok(wl_cobs_encode(boundary_native, raw_len,
              overlap_expected, sizeof(overlap_expected) - 1U, &expected_len));
          overlap_expected[expected_len++] = 0U;
          for (unsigned exact = 0U; exact < 2U; ++exact) {
            const size_t capacity = exact ? expected_len : sizeof(boundary_cobs);
            memset(boundary_cobs, 0xA5, sizeof(boundary_cobs));
            zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM,
                boundary_cobs, capacity, &actual_len));
            zassert_equal(actual_len, expected_len);
            zassert_mem_equal(boundary_cobs, overlap_expected, expected_len);
            for (size_t i = expected_len; i < sizeof(boundary_cobs); ++i)
              zassert_equal(boundary_cobs[i], 0xA5);

            /* Alias payload to output: this must retain the overlap-safe path. */
            memcpy(boundary_cobs, boundary_payload, packet.payload_len);
            packet.payload = boundary_cobs;
            zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM,
                boundary_cobs, capacity, &actual_len));
            zassert_equal(actual_len, expected_len);
            zassert_mem_equal(boundary_cobs, overlap_expected, expected_len);
            packet.payload = boundary_payload;
          }
          memset(boundary_cobs, 0xA5, sizeof(boundary_cobs));
          actual_len = SIZE_MAX;
          zassert_equal(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM,
              boundary_cobs, expected_len - 1U, &actual_len), WL_ERR_BUF_TOO_SMALL);
          zassert_equal(actual_len, 0U);
          for (size_t i = 0U; i < sizeof(boundary_cobs); ++i)
            zassert_equal(boundary_cobs[i], 0xA5);
          zassert_equal(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM,
              NULL, sizeof(boundary_cobs), &actual_len), WL_ERR_INVALID_ARG);
        }
      }
    }
  }
}

ZTEST(wirelink_frame_unit, test_encode_decode_roundtrip_crc32c)
{
  const uint8_t payload[] = {0x10, 0x20, 0x00, 0x30, 0x40, 0x50};
  uint8_t encoded[WL_FRAME_MAX_RAW_LEN] = {0};
  size_t encoded_len = 0;
  wl_frame_view_t view = {0};

  wl_wire_packet_t packet = {0};
  packet.type = WL_PACKET_DATA;
  packet.flags = 0;
  packet.message_id = 1U;
  packet.session_id = 0x0102030405060708ULL;
  packet.sequence = 42U;
  packet.integrity = WL_INTEGRITY_CRC32C;
  packet.payload = payload;
  packet.payload_len = sizeof(payload);

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                            sizeof(encoded), &encoded_len));
  zassert_equal(packet.payload_len + WL_FRAME_BASE_HEADER_SIZE + 4U,
                encoded_len);

  zassert_ok(wl_frame_decode(encoded, encoded_len, WL_INTEGRITY_CRC32C, &view));
  zassert_equal(view.type, WL_PACKET_DATA);
  zassert_equal(view.flags, packet.flags);
  zassert_equal(view.message_id, packet.message_id);
  zassert_equal(view.session_id, 0U);
  zassert_equal(view.sequence, 0U);
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
  packet.flags = WL_PACKET_FLAG_RELIABLE;
  packet.message_id = 1U;
  packet.session_id = 1ULL;
  packet.sequence = 1U;
  packet.integrity = WL_INTEGRITY_NONE;
  packet.payload = payload;
  packet.payload_len = sizeof(payload);

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                            sizeof(encoded), &encoded_len));

  memset(encoded + 4U, 0, sizeof(uint64_t));
  zassert_equal(wl_frame_decode(encoded, encoded_len, WL_INTEGRITY_NONE, &view),
                WL_ERR_BAD_FRAME);

  encoded[1] = 0x80;
  zassert_equal(wl_frame_decode(encoded, encoded_len, WL_INTEGRITY_NONE, &view),
                WL_ERR_BAD_FRAME);
}

ZTEST(wirelink_frame_unit, test_encode_rejects_zero_session)
{
  wl_wire_packet_t packet = {0};
  uint8_t encoded[WL_FRAME_MAX_RAW_LEN] = {0};
  size_t encoded_len = 0;

  packet.type = WL_PACKET_DATA;
  packet.flags = WL_PACKET_FLAG_RELIABLE;
  packet.message_id = 1U;
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
  packet.message_id = 1U;
  packet.session_id = 0x0102030405060708ULL;
  packet.sequence = 1U;
  packet.integrity = WL_INTEGRITY_NONE;
  packet.payload = NULL;
  packet.payload_len = 0U;

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                            sizeof(encoded), &encoded_len));

  encoded[0] = (uint8_t)(WL_FRAME_PREFIX | WL_FRAME_KIND_MASK);
  zassert_equal(wl_frame_decode(encoded, encoded_len, WL_INTEGRITY_NONE, &view),
                WL_ERR_NOT_SUPPORTED);
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
  packet.message_id = 7U;
  packet.session_id = 0x0102030405060708ULL;
  packet.sequence = 3U;
  packet.integrity = WL_INTEGRITY_CRC16;
  packet.payload = payload;
  packet.payload_len = sizeof(payload);

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                            sizeof(encoded), &encoded_len));

  encoded[WL_FRAME_BASE_HEADER_SIZE + 1U] ^= 0xFFU;
  zassert_equal(wl_frame_decode(encoded, encoded_len, WL_INTEGRITY_CRC16, &view),
                WL_ERR_CRC);
}

ZTEST(wirelink_frame_unit, test_envelopes_match_at_cobs_and_payload_boundaries)
{
  static const size_t lengths[] = {0U,   1U,   231U, 232U, 233U, 253U,
                                   254U, 255U, 507U, 508U, 509U,
                                   WL_FRAME_MAX_PAYLOAD};

  for (size_t pattern = 0U; pattern < 2U; ++pattern) {
    for (size_t i = 0U; i < sizeof(boundary_payload); ++i) {
      uint8_t value = (uint8_t)((i % 251U) + 1U);
      boundary_payload[i] =
          (pattern != 0U && (i % 17U) == 0U) ? 0U : value;
    }

    for (size_t integrity = WL_INTEGRITY_NONE;
         integrity <= WL_INTEGRITY_CRC32C; ++integrity) {
      for (size_t index = 0U; index < ARRAY_SIZE(lengths); ++index) {
        wl_wire_packet_t packet = {
            .type = WL_PACKET_DATA,
            .integrity = (wl_integrity_t)integrity,
            .flags = WL_PACKET_FLAG_RELIABLE,
            .message_id = 0xFEFFU,
            .session_id = UINT64_C(0x0102030405060708),
            .sequence = UINT32_C(0xF1F2F3F4),
            .payload = boundary_payload,
            .payload_len = lengths[index],
        };
        wl_frame_view_t view = {0};
        size_t native_len = 0U;
        size_t length16_len = 0U;
        size_t cobs_len = 0U;
        size_t decoded_len = 0U;

        zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET,
                                   boundary_native, sizeof(boundary_native),
                                   &native_len));
        zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_BUS_LENGTH16,
                                   boundary_length16,
                                   sizeof(boundary_length16), &length16_len));
        zassert_equal(length16_len, native_len + 2U);
        zassert_equal(((size_t)boundary_length16[0] << 8U) |
                          boundary_length16[1],
                      native_len);
        zassert_mem_equal(boundary_length16 + 2U, boundary_native, native_len);

        zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM,
                                   boundary_cobs, sizeof(boundary_cobs),
                                   &cobs_len));
        zassert_true(cobs_len >= 2U);
        zassert_equal(boundary_cobs[cobs_len - 1U], 0U);
        zassert_ok(wl_cobs_decode(boundary_cobs, cobs_len - 1U,
                                  boundary_decoded, sizeof(boundary_decoded),
                                  &decoded_len));
        zassert_equal(decoded_len, native_len);
        zassert_mem_equal(boundary_decoded, boundary_native, native_len);

        zassert_ok(wl_frame_decode(boundary_native, native_len,
                                   packet.integrity, &view));
        zassert_equal(view.payload.length, packet.payload_len);
        zassert_mem_equal(view.payload.data, packet.payload,
                          packet.payload_len);
      }
    }
  }
}

ZTEST(wirelink_frame_unit, test_encode_capacity_failure_does_not_write)
{
  static const uint8_t payload[] = {0x00U, 0x01U, 0x00U, 0x02U, 0x03U};
  uint8_t encoded[WL_FRAME_MAX_COBS_LEN];
  wl_wire_packet_t packet = {
      .type = WL_PACKET_DATA,
      .integrity = WL_INTEGRITY_CRC32C,
      .flags = 0U,
      .message_id = 1U,
      .session_id = UINT64_C(0x0102030405060708),
      .sequence = 1U,
      .payload = payload,
      .payload_len = sizeof(payload),
  };

  for (size_t envelope = WL_ENVELOPE_COBS_STREAM;
       envelope <= WL_ENVELOPE_BUS_LENGTH16; ++envelope) {
    size_t required = 0U;
    size_t encoded_len = SIZE_MAX;

    zassert_ok(wl_frame_encode(&packet, (wl_envelope_type_t)envelope, encoded,
                               sizeof(encoded), &required));
    zassert_true(required > 0U);
    memset(encoded, 0xA5, required);

    zassert_equal(wl_frame_encode(&packet, (wl_envelope_type_t)envelope,
                                  encoded, required - 1U, &encoded_len),
                  WL_ERR_BUF_TOO_SMALL);
    zassert_equal(encoded_len, 0U);
    for (size_t i = 0U; i < required; ++i) {
      zassert_equal(encoded[i], 0xA5U, "byte %zu changed", i);
    }

    zassert_ok(wl_frame_encode(&packet, (wl_envelope_type_t)envelope, encoded,
                               required, &encoded_len));
    zassert_equal(encoded_len, required);
  }
}

ZTEST(wirelink_frame_unit, test_encode_preserves_overlapping_payload)
{
  static const struct {
    size_t payload_offset;
    size_t output_offset;
  } layouts[] = {
      {0U, 0U}, {64U, 0U}, {0U, 64U}, {128U, 64U}, {64U, 128U},
      {1U, 0U}, {0U, 1U}, {1U, 1U}, {2U, 3U}, {3U, 2U}, {5U, 7U}, {7U, 5U}};
  wl_wire_packet_t packet = {
      .type = WL_PACKET_DATA,
      .integrity = WL_INTEGRITY_CRC32C,
      .flags = WL_PACKET_FLAG_RELIABLE,
      .message_id = 0x1234U,
      .session_id = UINT64_C(0x0102030405060708),
      .sequence = UINT32_C(0xF1F2F3F4),
      .payload = overlap_payload,
      .payload_len = sizeof(overlap_payload),
  };

  for (size_t i = 0U; i < sizeof(overlap_payload); ++i) {
    overlap_payload[i] =
        ((i % 257U) == 0U) ? 0U : (uint8_t)((i % 251U) + 1U);
  }

  for (size_t envelope = WL_ENVELOPE_COBS_STREAM;
       envelope <= WL_ENVELOPE_BUS_LENGTH16; ++envelope) {
    size_t expected_len = 0U;

    packet.payload = overlap_payload;
    zassert_ok(wl_frame_encode(&packet, (wl_envelope_type_t)envelope,
                               overlap_expected, sizeof(overlap_expected),
                               &expected_len));

    for (size_t layout = 0U; layout < ARRAY_SIZE(layouts); ++layout) {
      uint8_t *output = overlap_arena + layouts[layout].output_offset;
      size_t actual_len = 0U;

      memset(overlap_arena, 0xA5, sizeof(overlap_arena));
      memcpy(overlap_arena + layouts[layout].payload_offset, overlap_payload,
             sizeof(overlap_payload));
      packet.payload = overlap_arena + layouts[layout].payload_offset;

      zassert_ok(wl_frame_encode(
          &packet, (wl_envelope_type_t)envelope, output,
          sizeof(overlap_arena) - layouts[layout].output_offset, &actual_len),
                 "envelope %zu layout %zu", envelope, layout);
      zassert_equal(actual_len, expected_len);
      zassert_mem_equal(output, overlap_expected, expected_len,
                        "envelope %zu layout %zu", envelope, layout);
    }
  }
}

ZTEST(wirelink_frame_unit, test_packet_envelopes_accept_unaligned_output)
{
  static const size_t lengths[] = {0U, 1U, 3U, 32U, 255U, WL_FRAME_MAX_PAYLOAD};
  for (size_t i = 0U; i < sizeof(boundary_payload); ++i) {
    boundary_payload[i] = (uint8_t)i;
  }
  for (unsigned kind = 0U; kind < 3U; ++kind) {
    for (int integrity = WL_INTEGRITY_NONE; integrity <= WL_INTEGRITY_CRC32C; ++integrity) {
      for (int envelope = WL_ENVELOPE_NATIVE_PACKET; envelope <= WL_ENVELOPE_BUS_LENGTH16; ++envelope) {
        for (size_t size = 0U; size < (kind == 2U ? 1U : ARRAY_SIZE(lengths)); ++size) {
          wl_wire_packet_t packet = {
              .type = kind == 2U ? WL_PACKET_ACK : WL_PACKET_DATA,
              .flags = kind == 1U ? WL_PACKET_FLAG_RELIABLE : 0U,
              .integrity = integrity, .message_id = kind == 2U ? 0U : 21U,
              .session_id = UINT64_C(0x123456789ABCDEF0), .sequence = 255U,
              .payload = boundary_payload, .payload_len = lengths[size]};
          size_t expected_len, actual_len;
          zassert_ok(wl_frame_encode(&packet, envelope, overlap_expected,
              sizeof(overlap_expected), &expected_len));
          for (size_t offset = 1U; offset <= 7U; ++offset) {
            memset(overlap_arena, 0xA5, sizeof(overlap_arena));
            uint8_t *output = overlap_arena + offset;
            zassert_equal(wl_frame_encode(&packet, envelope, output,
                expected_len - 1U, &actual_len), WL_ERR_BUF_TOO_SMALL);
            zassert_equal(actual_len, 0U);
            for (size_t i = 0U; i < sizeof(overlap_arena); ++i) {
              zassert_equal(overlap_arena[i], 0xA5U);
            }
            zassert_ok(wl_frame_encode(&packet, envelope, output,
                expected_len, &actual_len));
            zassert_equal(actual_len, expected_len);
            zassert_mem_equal(output, overlap_expected, expected_len);
            for (size_t i = 0U; i < offset; ++i) {
              zassert_equal(overlap_arena[i], 0xA5U);
            }
            for (size_t i = offset + expected_len; i < sizeof(overlap_arena); ++i) {
              zassert_equal(overlap_arena[i], 0xA5U);
            }
          }
        }
      }
    }
  }
}

ZTEST(wirelink_frame_unit, test_frame_api_rejects_selector_out_of_range)
{
  uint8_t encoded[WL_FRAME_MAX_RAW_LEN];
  size_t encoded_len = SIZE_MAX;
  wl_frame_view_t view = {0};
  wl_wire_packet_t packet = {
      .type = WL_PACKET_DATA,
      .integrity = WL_INTEGRITY_NONE,
      .flags = 0U,
      .message_id = 1U,
      .session_id = 1U,
      .sequence = 1U,
      .payload = NULL,
      .payload_len = 0U,
  };

  zassert_equal(wl_frame_encode(&packet, (wl_envelope_type_t)-1, encoded,
                                sizeof(encoded), &encoded_len),
                WL_ERR_INVALID_ARG);
  zassert_equal(encoded_len, 0U);
  zassert_equal(wl_frame_encode(&packet, (wl_envelope_type_t)3, encoded,
                                sizeof(encoded), &encoded_len),
                WL_ERR_INVALID_ARG);

  packet.integrity = (wl_integrity_t)-1;
  zassert_equal(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                                sizeof(encoded), &encoded_len),
                WL_ERR_INVALID_ARG);
  packet.integrity = (wl_integrity_t)3;
  zassert_equal(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                                sizeof(encoded), &encoded_len),
                WL_ERR_INVALID_ARG);

  zassert_equal(wl_frame_decode(encoded, sizeof(encoded),
                                (wl_integrity_t)-1, &view),
                WL_ERR_INVALID_ARG);
  zassert_equal(wl_frame_decode(encoded, sizeof(encoded), (wl_integrity_t)3,
                                &view),
                WL_ERR_INVALID_ARG);

  zassert_equal(wl_frame_overhead((wl_integrity_t)-1), 0U);
  zassert_equal(wl_frame_overhead((wl_integrity_t)3), 0U);
  zassert_equal(wl_frame_raw_size(1U, (wl_integrity_t)-1), 0U);
  zassert_equal(wl_frame_raw_size(1U, (wl_integrity_t)3), 0U);
  zassert_equal(wl_frame_encode_overhead((wl_envelope_type_t)-1,
                                         WL_INTEGRITY_NONE),
                0U);
  zassert_equal(wl_frame_encode_overhead((wl_envelope_type_t)3,
                                         WL_INTEGRITY_NONE),
                0U);
  zassert_equal(wl_frame_encode_overhead(WL_ENVELOPE_NATIVE_PACKET,
                                         (wl_integrity_t)-1),
                0U);
  zassert_equal(wl_frame_encode_overhead(WL_ENVELOPE_NATIVE_PACKET,
                                         (wl_integrity_t)3),
                0U);
}

ZTEST_SUITE(wirelink_frame_unit, NULL, NULL, NULL, NULL, NULL);
