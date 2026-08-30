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
  /* Sequence existed in v1; field 5 was added compatibly in v2. */
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

ZTEST_SUITE(wirelink_generated_codec, NULL, NULL, NULL, NULL, NULL);
