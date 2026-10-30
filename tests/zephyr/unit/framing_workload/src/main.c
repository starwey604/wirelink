/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <zephyr/ztest.h>
#include "wirelink/cobs.h"
#include "wirelink/crc.h"
#include "wirelink/frame.h"
#include "workload.h"
#include "frame_internal.h"

static size_t encode_calls;
void __cyg_profile_func_enter(void *function, void *caller) {
  (void)caller;
  if ((uintptr_t)function == (uintptr_t)&wl_frame_encode) ++encode_calls;
}
void __cyg_profile_func_exit(void *function, void *caller) {
  (void)function;
  (void)caller;
}

/* Independent bitwise CRCs and raw header construction, without calling the
 * production frame encoder to obtain expected bytes. */
static uint32_t reference_crc(const uint8_t *data, size_t length, wl_integrity_t integrity) {
  uint32_t crc = integrity == WL_INTEGRITY_CRC16 ? 0xffffU : 0xffffffffU;
  for (size_t i = 0U; i < length; ++i) {
    if (integrity == WL_INTEGRITY_CRC16) {
      crc ^= (uint32_t)data[i] << 8U;
      for (unsigned bit = 0U; bit < 8U; ++bit)
        crc = ((crc << 1U) ^ ((crc & 0x8000U) ? 0x1021U : 0U)) & 0xffffU;
    } else {
      crc ^= data[i];
      for (unsigned bit = 0U; bit < 8U; ++bit)
        crc = (crc >> 1U) ^ ((crc & 1U) ? 0x82f63b78U : 0U);
    }
  }
  return integrity == WL_INTEGRITY_CRC16 ? crc : crc ^ 0xffffffffU;
}

static void put_be(uint8_t *out, uint64_t value, size_t length) {
  for (size_t i = 0U; i < length; ++i)
    out[i] = (uint8_t)(value >> ((length - i - 1U) * 8U));
}

static size_t raw_reference(const wl_wire_packet_t *p, uint8_t *raw) {
  unsigned kind = p->type == WL_PACKET_ACK ? 2U : p->flags != 0U ? 1U : 0U;
  size_t length = kind != 0U ? 16U : 4U;
  raw[0] = (uint8_t)(0xa4U | kind);
  raw[1] = 0U;
  put_be(raw + 2U, p->message_id, 2U);
  if (kind != 0U) {
    put_be(raw + 4U, p->session_id, 8U);
    put_be(raw + 12U, p->sequence, 4U);
  }
  if (p->payload_len != 0U) memcpy(raw + length, p->payload, p->payload_len);
  length += p->payload_len;
  if (p->integrity != WL_INTEGRITY_NONE) {
    size_t crc_length = p->integrity == WL_INTEGRITY_CRC16 ? 2U : 4U;
    put_be(raw + length, reference_crc(raw, length, p->integrity), crc_length);
    length += crc_length;
  }
  return length;
}

static size_t envelop(const uint8_t *raw, size_t length, wl_envelope_type_t envelope, uint8_t *out) {
  if (envelope == WL_ENVELOPE_COBS_STREAM) {
    size_t size;
    zassert_ok(wl_cobs_encode(raw, length, out, WL_FRAME_MAX_COBS_LEN - 1U, &size));
    out[size++] = 0U;
    return size;
  }
  size_t prefix = envelope == WL_ENVELOPE_BUS_LENGTH16 ? 2U : 0U;
  if (prefix != 0U) put_be(out, length, 2U);
  memcpy(out + prefix, raw, length);
  return length + prefix;
}


ZTEST(wirelink_framing_workload, test_invalid_selectors_and_failure_outputs) {
  static uint8_t bytes[WL_FRAME_MAX_COBS_LEN];
  for (int e = -1; e <= 3; ++e) for (int i = -1; i <= 3; ++i) {
    const int valid = e >= 0 && e <= 2 && i >= 0 && i <= 2;
    int expected = !valid ? WL_ERR_INVALID_ARG : WL_OK;
    wl_wire_packet_t p = {.type = WL_PACKET_DATA, .message_id = 1U, .integrity = i};
    size_t length = 42U;
    memset(bytes, 0x5a, sizeof(bytes));
    zassert_equal(wl_frame_encode(&p, e, bytes, sizeof(bytes), &length), expected);
    if (expected != WL_OK) {
      zassert_equal(length, 0U);
      zassert_equal(bytes[0], 0x5aU);
      zassert_equal(wl_frame_encode_overhead(e, i), 0U);
    } else zassert_true(wl_frame_encode_overhead(e, i) != 0U);
  }
  for (int i = -1; i <= 3; ++i) {
    wl_frame_view_t view, saved;
    memset(&view, 0x5a, sizeof(view)); saved = view;
    const uint8_t raw[] = {0xa4U, 0U, 0U, 1U};
    int expected = i < 0 || i > 2 ? WL_ERR_INVALID_ARG :
        i == 0 ? WL_OK : WL_ERR_BAD_FRAME;
    zassert_equal(wl_frame_decode(raw, sizeof(raw), i, &view), expected);
    if (expected != WL_OK) zassert_mem_equal(&view, &saved, sizeof(view));
    if (i < 0 || i > 2) {
      zassert_equal(wl_frame_overhead(i), 0U);
      zassert_equal(wl_frame_raw_size(1U, i), 0U);
    }
  }
  const uint8_t check[] = "123456789";
  zassert_equal(wl_crc16_ccitt_false(check, 9U), 0x29b1U);
  zassert_equal(wl_crc32c(check, 9U), 0xe3069283U);
}

ZTEST(wirelink_framing_workload, test_bytes_capacities_aliases_and_unaligned) {
  static uint8_t payload[WL_FRAME_MAX_PAYLOAD], raw[WL_FRAME_MAX_RAW_LEN];
  static uint8_t expected[WL_FRAME_MAX_COBS_LEN], arena[WL_FRAME_MAX_COBS_LEN * 3U];
  static uint8_t saved[sizeof(arena)];
  const size_t lengths[] = {0U, 1U, 2U, 3U, 16U, 24U, 32U, 120U, 253U, 254U,
      255U, 508U, 512U, 2048U};
  uint32_t random = 0x12345678U;
  for (int e = 0; e <= 2; ++e) for (int i = 0; i <= 2; ++i) {
    for (unsigned pattern = 0U; pattern < 4U; ++pattern) {
      for (size_t b = 0U; b < sizeof(payload); ++b) {
        random = random * 1664525U + 1013904223U;
        payload[b] = pattern == 0U ? 0U : pattern == 1U ? (uint8_t)(b % 255U + 1U) :
            pattern == 2U ? (b % 17U == 0U ? 0U : 1U) : (uint8_t)(random >> 24U);
      }
      for (unsigned kind = 0U; kind < 3U; ++kind) {
        for (size_t n = 0U; n < (kind == 2U ? 1U : ARRAY_SIZE(lengths)); ++n) {
          wl_wire_packet_t p = {.type = kind == 2U ? WL_PACKET_ACK : WL_PACKET_DATA,
              .integrity = i, .flags = kind == 1U ? WL_PACKET_FLAG_RELIABLE : 0U,
              .message_id = kind == 2U ? 0U : 21U, .session_id = UINT64_C(0x123456789abcdef0),
              .sequence = 0xfedcba98U, .payload = payload, .payload_len = lengths[n]};
          size_t raw_length = raw_reference(&p, raw);
          size_t expected_length = envelop(raw, raw_length, e, expected);
          for (unsigned alias = 0U; alias < 4U; ++alias) for (unsigned capacity = 0U; capacity < 3U; ++capacity) {
            uint8_t *out = arena + WL_FRAME_MAX_COBS_LEN + 1U;
            uint8_t *input = alias == 0U ? payload : alias == 1U ? out : alias == 2U ? out - 17U : out + 17U;
            memset(arena, 0xa5, sizeof(arena));
            if (alias != 0U && p.payload_len != 0U) memcpy(input, payload, p.payload_len);
            memcpy(saved, arena, sizeof(arena));
            p.payload = input;
            size_t length = 99U;
            size_t cap = capacity == 0U ? expected_length - 1U : capacity == 1U ? expected_length : WL_FRAME_MAX_COBS_LEN;
            int error = wl_frame_encode(&p, e, out, cap, &length);
            if (capacity == 0U) {
              zassert_equal(error, WL_ERR_BUF_TOO_SMALL);
              zassert_equal(length, 0U);
              zassert_mem_equal(arena, saved, sizeof(arena));
            } else {
              zassert_ok(error);
              zassert_equal(length, expected_length);
              zassert_mem_equal(out, expected, length);
              zassert_mem_equal(arena, saved, (size_t)(out - arena));
              zassert_mem_equal(out + length, saved + (out - arena) + length, sizeof(arena) - (size_t)(out - arena) - length);
            }
          }
          wl_frame_view_t view;
          zassert_ok(wl_frame_decode(raw, raw_length, i, &view));
          zassert_equal(view.payload.length, p.payload_len);
          zassert_mem_equal(view.payload.data, payload, p.payload_len);
        }
      }
    }
  }
}

ZTEST(wirelink_framing_workload, test_independent_framing_workloads) {
  static framing_fixture_t f;
  const size_t lengths[] = {0U, 32U, 120U, 254U, 512U, 2048U};
  for (unsigned mode = 0U; mode < FRAMING_MODES; ++mode)
    for (int e = 0; e <= 2; ++e) for (int i = 0; i <= 2; ++i) {
      if ((mode == FRAMING_RAW_DECODE || mode == FRAMING_COBS_DECODE) && e != 0) continue;
      for (unsigned pattern = 0U; pattern < 3U; ++pattern)
        for (size_t n = 0U; n < ARRAY_SIZE(lengths); ++n) {
          zassert_ok(framing_init(&f, mode, e, i, pattern, lengths[n]));
          for (unsigned repeat = 0U; repeat < 3U; ++repeat) {
            size_t before_calls = f.calls;
            size_t before_encode = encode_calls;
            zassert_ok(framing_step(&f));
            zassert_ok(framing_check(&f));
            zassert_equal(f.calls - before_calls, 0U);
            zassert_equal(encode_calls - before_encode, mode < FRAMING_RAW_DECODE ? 1U : 0U);
          }
        }
    }
}

ZTEST_SUITE(wirelink_framing_workload, NULL, NULL, NULL, NULL, NULL);
