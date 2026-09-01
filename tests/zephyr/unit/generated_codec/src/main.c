/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "control.h"

static bool points_into(const void *pointer, size_t length,
                        const uint8_t *storage, size_t storage_length) {
  const uintptr_t address = (uintptr_t)pointer;
  const uintptr_t begin = (uintptr_t)storage;

  return address >= begin && length <= storage_length &&
         address - begin <= storage_length - length;
}

static void prepare_decode(arm_command_t *command, joint_command_t *joints,
                           size_t capacity) {
  memset(command, 0, sizeof(*command));
  memset(joints, 0, capacity * sizeof(*joints));
  command->joints = joints;
  command->joints_capacity = capacity;
}

ZTEST(wirelink_generated_codec, test_schema_evolution_payload)
{
  /* Sequence existed in v1; field 5 was added compatibly after v1. */
  const uint8_t current_payload[] = {0x10U, 0x07U, 0x28U, 0x01U};
  arm_command_t decoded;
  joint_command_t joints[1];

  prepare_decode(&decoded, joints, 1U);
  zassert_equal(arm_command_decode(current_payload, sizeof(current_payload),
                                   &decoded),
                WL_CODEC_OK);
  zassert_true(decoded.has_sequence);
  zassert_equal(decoded.sequence, 7U);
#if defined(WLC_SCHEMA_CURRENT)
  zassert_true(decoded.has_enabled);
  zassert_true(decoded.enabled);

  /* A v2 decoder accepts a v1 payload and applies the declared default. */
  const uint8_t previous_payload[] = {0x10U, 0x09U};
  prepare_decode(&decoded, joints, 1U);
  zassert_equal(arm_command_decode(previous_payload, sizeof(previous_payload),
                                   &decoded),
                WL_CODEC_OK);
  zassert_true(decoded.has_sequence);
  zassert_equal(decoded.sequence, 9U);
  zassert_false(decoded.has_enabled);
  zassert_true(decoded.enabled);
#else
  /* The v1 decoder skipped the unknown v2 field without losing known data. */
  zassert_equal(decoded.joints_count, 0U);
#endif
}

ZTEST(wirelink_generated_codec, test_length_delimited_fields_are_borrowed)
{
  const uint8_t payload[] = {
      0x1AU, 0x04U, 'h', 'o', 's', 't',
      0x22U, 0x03U, 0x00U, 0xA5U, 0xFFU,
  };
  arm_command_t decoded;
  joint_command_t joints[1];

  prepare_decode(&decoded, joints, 1U);
  zassert_equal(arm_command_decode(payload, sizeof(payload), &decoded),
                WL_CODEC_OK);
  zassert_true(decoded.has_source);
  zassert_equal(decoded.source.length, 4U);
  zassert_mem_equal(decoded.source.data, "host", 4U);
  zassert_true(points_into(decoded.source.data, decoded.source.length, payload,
                           sizeof(payload)));
  zassert_true(decoded.has_extension);
  zassert_equal(decoded.extension.length, 3U);
  zassert_true(points_into(decoded.extension.data, decoded.extension.length,
                           payload, sizeof(payload)));
}

ZTEST(wirelink_generated_codec, test_decode_errors_are_deterministic)
{
  const uint8_t capacity[] = {0x0AU, 0x00U, 0x0AU, 0x00U};
  const uint8_t invalid_utf8[] = {0x1AU, 0x02U, 0xC0U, 0x80U};
  const uint8_t duplicate[] = {0x10U, 0x01U, 0x10U, 0x02U};
  const uint8_t wrong_wire[] = {0x18U, 0x01U};
  const uint8_t malformed[] = {0x80U};
  arm_command_t decoded;
  joint_command_t joints[1];

  prepare_decode(&decoded, joints, 1U);
  zassert_equal(arm_command_decode(capacity, sizeof(capacity), &decoded),
                WL_CODEC_ERR_CAPACITY);
  prepare_decode(&decoded, joints, 1U);
  zassert_equal(arm_command_decode(invalid_utf8, sizeof(invalid_utf8), &decoded),
                WL_CODEC_ERR_UTF8);
  prepare_decode(&decoded, joints, 1U);
  zassert_equal(arm_command_decode(duplicate, sizeof(duplicate), &decoded),
                WL_CODEC_ERR_DUPLICATE_FIELD);
  prepare_decode(&decoded, joints, 1U);
  zassert_equal(arm_command_decode(wrong_wire, sizeof(wrong_wire), &decoded),
                WL_CODEC_ERR_WIRE_TYPE);
  prepare_decode(&decoded, joints, 1U);
  zassert_equal(arm_command_decode(malformed, sizeof(malformed), &decoded),
                WL_CODEC_ERR_MALFORMED);
}

#if defined(WLC_SCHEMA_CURRENT)
static void set_float_bits(float *value, uint32_t bits) {
  memcpy(value, &bits, sizeof(bits));
}

static uint32_t get_float_bits(const float *value) {
  uint32_t bits;

  memcpy(&bits, value, sizeof(bits));
  return bits;
}

ZTEST(wirelink_generated_codec, test_dense_float_control_is_compact_and_bit_exact)
{
  static const uint32_t first_bits[] = {
      UINT32_C(0x00000000), /* +0 */
      UINT32_C(0x80000000), /* -0 */
      UINT32_C(0x7FC12345), /* quiet NaN with a non-canonical payload */
      UINT32_C(0x3F800000), /* 1.0 */
  };
  static const uint8_t positive_zero_bytes[] = {0x00U, 0x00U, 0x00U, 0x00U};
  static const uint8_t negative_zero_bytes[] = {0x80U, 0x00U, 0x00U, 0x00U};
  static const uint8_t nan_bytes[] = {0x7FU, 0xC1U, 0x23U, 0x45U};
  const uint8_t malformed_length[] = {0x0AU, 0x04U, 0U, 0U, 0U, 0U};
  arm_mit_command_t command = {0};
  arm_mit_command_t decoded = {0};
  uint8_t payload[160];
  size_t payload_length = 0U;

  arm_mit_command_clear(&command);
  command.has_controls = true;
  for (size_t i = 0U; i < ARRAY_SIZE(command.controls); ++i) {
    const uint32_t bits =
        i < ARRAY_SIZE(first_bits) ? first_bits[i]
                                   : UINT32_C(0x3F000000) + (uint32_t)i;
    set_float_bits(&command.controls[i], bits);
  }

  /* One key, one 120-byte length, and thirty binary32 values. */
  zassert_equal(arm_mit_command_encoded_size(&command), 122U);
  zassert_ok(arm_mit_command_encode(&command, payload, sizeof(payload),
                                    &payload_length));
  zassert_equal(payload_length, 122U);
  zassert_equal(payload[0], 0x0AU);
  zassert_equal(payload[1], 0x78U);
  zassert_mem_equal(&payload[2], positive_zero_bytes,
                    sizeof(positive_zero_bytes));
  zassert_mem_equal(&payload[6], negative_zero_bytes,
                    sizeof(negative_zero_bytes));
  zassert_mem_equal(&payload[10], nan_bytes, sizeof(nan_bytes));

  zassert_ok(
      arm_mit_command_decode(payload, payload_length, &decoded));
  zassert_true(decoded.has_controls);
  zassert_mem_equal(decoded.controls, command.controls,
                    sizeof(command.controls));
  zassert_equal(get_float_bits(&decoded.controls[0]), first_bits[0]);
  zassert_equal(get_float_bits(&decoded.controls[1]), first_bits[1]);
  zassert_equal(get_float_bits(&decoded.controls[2]), first_bits[2]);

  command.has_dt_s = true;
  set_float_bits(&command.dt_s, UINT32_C(0xBA83126F));
  zassert_ok(arm_mit_command_encode(&command, payload, sizeof(payload),
                                    &payload_length));
  zassert_equal(payload_length, 127U);
  zassert_ok(
      arm_mit_command_decode(payload, payload_length, &decoded));
  zassert_true(decoded.has_dt_s);
  zassert_equal(get_float_bits(&decoded.dt_s), UINT32_C(0xBA83126F));

  zassert_equal(arm_mit_command_decode(malformed_length,
                                       sizeof(malformed_length), &decoded),
                WL_CODEC_ERR_MALFORMED);
}

ZTEST(wirelink_generated_codec, test_rpc_messages_carry_operation_identity)
{
  static const uint8_t expected[] = {0x08U, 0xA3U, 0x02U, 0x10U, 0x3FU};
  home_request_t request;
  home_request_t decoded;
  home_response_t response;
  uint8_t payload[16];
  size_t payload_length = 0U;

  home_request_clear(&request);
  request.has_operation_id = true;
  request.operation_id = 0x123U;
  request.has_joint_mask = true;
  request.joint_mask = 0x3FU;
  zassert_equal(home_request_encode(&request, payload, sizeof(payload),
                                    &payload_length),
                WL_CODEC_OK);
  zassert_equal(payload_length, sizeof(expected));
  zassert_mem_equal(payload, expected, sizeof(expected));
  zassert_equal(home_request_decode(payload, payload_length, &decoded),
                WL_CODEC_OK);
  zassert_true(decoded.has_operation_id);
  zassert_equal(decoded.operation_id, request.operation_id);
  zassert_true(decoded.has_joint_mask);
  zassert_equal(decoded.joint_mask, request.joint_mask);

  home_response_clear(&response);
  response.has_operation_id = true;
  response.operation_id = request.operation_id;
  response.has_status = true;
  response.status = OPERATION_REJECTED;
  zassert_equal(home_response_encode(&response, payload, sizeof(payload),
                                     &payload_length),
                WL_CODEC_OK);
  zassert_equal(home_response_decode(payload, payload_length, &response),
                WL_CODEC_OK);
  zassert_true(response.has_operation_id);
  zassert_equal(response.operation_id, request.operation_id);
  zassert_true(response.has_status);
  zassert_equal(response.status, OPERATION_REJECTED);
}

ZTEST(wirelink_generated_codec, test_bounded_bulk_chunk_rejects_oversize)
{
  static uint8_t chunk_bytes[4097];
  static uint8_t encoded[BULK_CHUNK_MAX_ENCODED_SIZE];
  static const uint8_t oversized_wire_length[] = {
      0x1AU, 0x81U, 0x20U, /* data length = 4097 */
  };
  bulk_chunk_t chunk;
  bulk_chunk_t decoded;
  size_t encoded_length = 123U;

  zassert_equal(BULK_CHUNK_HAS_MAX_ENCODED_SIZE, 1);
  zassert_equal(BULK_CHUNK_MAX_ENCODED_SIZE, 4113U);

  bulk_chunk_clear(&chunk);
  chunk.has_transfer_id = true;
  chunk.transfer_id = 1U;
  chunk.has_offset = true;
  chunk.offset = 0U;
  chunk.has_data = true;
  chunk.data.data = chunk_bytes;
  chunk.data.length = sizeof(chunk_bytes);
  zassert_equal(bulk_chunk_encoded_size(&chunk), SIZE_MAX);
  zassert_equal(bulk_chunk_encode(&chunk, encoded, sizeof(encoded),
                                  &encoded_length),
                WL_CODEC_ERR_INVALID_VALUE);
  zassert_equal(encoded_length, 123U);

  zassert_equal(bulk_chunk_decode(oversized_wire_length,
                                  sizeof(oversized_wire_length), &decoded),
                WL_CODEC_ERR_INVALID_VALUE);

  chunk.data.length = sizeof(chunk_bytes) - 1U;
  zassert_equal(bulk_chunk_encoded_size(&chunk),
                BULK_CHUNK_MAX_ENCODED_SIZE);
  zassert_equal(bulk_chunk_encode(&chunk, encoded, sizeof(encoded),
                                  &encoded_length),
                WL_CODEC_OK);
  zassert_equal(encoded_length, BULK_CHUNK_MAX_ENCODED_SIZE);
  zassert_equal(bulk_chunk_decode(encoded, encoded_length, &decoded),
                WL_CODEC_OK);
  zassert_true(decoded.has_data);
  zassert_equal(decoded.data.length, sizeof(chunk_bytes) - 1U);
  zassert_true(points_into(decoded.data.data, decoded.data.length, encoded,
                           encoded_length));
}
#endif

ZTEST_SUITE(wirelink_generated_codec, NULL, NULL, NULL, NULL, NULL);
