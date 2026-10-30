/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <zephyr/ztest.h>
#include "wirelink/cobs.h"
#include "wirelink/crc.h"
#include "wirelink/frame.h"
#include "wirelink/wirelink.h"
#include "workload.h"
#include "frame_internal.h"
#include "link_profile.h"

static size_t encode_calls;
void __cyg_profile_func_enter(void *function, void *caller) {
  (void)caller;
  if ((uintptr_t)function == (uintptr_t)&wl_frame_encode_cobs_crc16) ++encode_calls;
#if !WL_LINK_PROFILE_COBS_CRC16
  if ((uintptr_t)function == (uintptr_t)&wl_frame_encode_generic) ++encode_calls;
#endif
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


typedef struct {
  wl_ctx_t ctx;
  uint8_t payload[WL_FRAME_MAX_PAYLOAD], tx[WL_FRAME_MAX_COBS_LEN];
  uint8_t control[WL_FRAME_MAX_COBS_LEN], fifo[WL_FRAME_MAX_COBS_LEN];
  uint8_t fallback[WL_FRAME_MAX_COBS_LEN];
  const uint8_t *sent;
  size_t length, calls;
  wl_io_token_t token;
  wl_sink_result_t result;
} fixture_t;
static fixture_t peers[18];

static wl_config_t config(int envelope, int integrity, uint64_t session) {
  return (wl_config_t){.max_payload_len = WL_FRAME_MAX_PAYLOAD, .envelope = envelope,
      .integrity = integrity, .session_id = session, .max_retries = 3U, .ack_timeout_ms = 5U};
}

static wl_storage_t storage(fixture_t *f) {
  return (wl_storage_t){f->payload, sizeof(f->payload), f->tx, sizeof(f->tx),
      f->control, sizeof(f->control), f->fifo, sizeof(f->fifo), f->fallback, sizeof(f->fallback)};
}

static wl_sink_result_t sink(void *context, wl_io_token_t token,
                              const uint8_t *bytes, size_t length) {
  fixture_t *f = context;
  f->sent = bytes;
  f->length = length;
  f->token = token;
  ++f->calls;
  return f->result;
}

static void init(fixture_t *f, wl_config_t cfg) {
  memset(f, 0, sizeof(*f));
  wl_storage_t st = storage(f);
  zassert_ok(wl_init(&f->ctx, &cfg, &st));
  f->result = WL_SINK_SENT;
  zassert_ok(wl_set_sink(&f->ctx, sink, f));
}

ZTEST(wirelink_framing_workload, test_invalid_selectors_and_failure_outputs) {
  static uint8_t bytes[WL_FRAME_MAX_COBS_LEN];
  fixture_t *f = &peers[0];
  wl_storage_t st = storage(f);
  for (int e = -1; e <= 3; ++e) for (int i = -1; i <= 3; ++i) {
    const int valid = e >= 0 && e <= 2 && i >= 0 && i <= 2;
    int expected = !valid ? WL_ERR_INVALID_ARG : WL_OK;
    wl_config_t cfg = config(e, i, 1U);
    wl_storage_requirements_t req, saved;
    memset(&req, 0x5a, sizeof(req)); saved = req;
    zassert_equal(wl_config_requirements(&cfg, &req), expected);
    if (expected != WL_OK) zassert_mem_equal(&req, &saved, sizeof(req));
    zassert_equal(wl_init(&f->ctx, &cfg, &st), expected);
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

ZTEST(wirelink_framing_workload, test_multiple_endpoints_reliable_ack_and_duplicates) {
  /* Initialize every supported pair before using any: catches global mode state. */
  unsigned pairs = 0U;
  for (int e = 0; e <= 2; ++e) for (int i = 0; i <= 2; ++i) {
    init(&peers[pairs * 2U], config(e, i, pairs + 1U));
    init(&peers[pairs * 2U + 1U], config(e, i, pairs + 101U));
    ++pairs;
  }
  for (unsigned n = 0U; n < pairs; ++n) {
    fixture_t *a = &peers[n * 2U], *b = a + 1U;
    wl_event_t event;
    wl_tx_handle_t handle;
    wl_tx_result_t result;
    const uint8_t payload[] = {1U, 0U, 2U, 0xffU};
    zassert_ok(wl_send_unreliable(&a->ctx, 21U, payload, sizeof(payload)));
    zassert_ok(wl_feed_unit(&b->ctx, a->sent, a->length));
    zassert_ok(wl_poll(&b->ctx, 0U, &event));
    zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
    zassert_mem_equal(event.payload, payload, sizeof(payload));
    wl_event_release(&b->ctx, &event);
    zassert_ok(wl_poll(&a->ctx, 0U, &event));
    zassert_equal(event.type, WL_EVT_TX_SUCCESS);
    zassert_ok(wl_send_reliable(&a->ctx, 21U, payload, sizeof(payload), 0U, &handle));
    zassert_ok(wl_feed_unit(&b->ctx, a->sent, a->length));
    zassert_ok(wl_poll(&b->ctx, 0U, &event));
    zassert_equal(event.type, WL_EVT_RELIABLE_RX);
    zassert_mem_equal(event.payload, payload, sizeof(payload));
    zassert_equal(wl_poll(&b->ctx, 0U, &(wl_event_t){0}), WL_ERR_NO_DATA);
    wl_event_release(&b->ctx, &event);
    zassert_ok(wl_feed_unit(&a->ctx, b->sent, b->length));
    zassert_ok(wl_poll(&a->ctx, 1U, &event));
    zassert_equal(event.type, WL_EVT_TX_SUCCESS);
    zassert_equal(event.handle, handle);
    zassert_ok(wl_tx_take(&a->ctx, handle, &result));
    zassert_equal(result.result, WL_OK);
    zassert_ok(wl_feed_unit(&b->ctx, a->sent, a->length));
    zassert_equal(wl_poll(&b->ctx, 1U, &event), WL_ERR_NO_DATA);
    wl_rx_counters_t counters;
    zassert_ok(wl_rx_get_counters(&b->ctx, &counters));
    zassert_equal(counters.duplicate, 1U);
    zassert_equal(b->calls, 2U);
  }
}

ZTEST(wirelink_framing_workload, test_busy_async_drain_and_retry_reuse_image) {
  fixture_t *f = &peers[0];
  init(f, config(0, 1, 1U));
  f->result = WL_SINK_BUSY;
  encode_calls = 0U;
  wl_tx_handle_t handle;
  wl_event_t event;
  zassert_ok(wl_set_tx_drain_required(&f->ctx, 1U));
  zassert_ok(wl_send_reliable(&f->ctx, 21U, (const uint8_t *)"data", 4U, 0U, &handle));
  static uint8_t image[WL_FRAME_MAX_COBS_LEN];
  memcpy(image, f->sent, f->length);
  size_t length = f->length;
  zassert_equal(encode_calls, 1U);
  f->result = WL_SINK_STARTED;
  zassert_ok(wl_tx_writable(&f->ctx));
  zassert_equal(wl_poll(&f->ctx, 1U, &event), WL_ERR_NO_DATA);
  wl_io_token_t token = f->token;
  zassert_ok(wl_tx_consumed(&f->ctx, token, WL_OK));
  zassert_equal(wl_poll(&f->ctx, 100U, &event), WL_ERR_NO_DATA);
  zassert_equal(f->calls, 2U);
  zassert_ok(wl_tx_drained(&f->ctx, token, WL_OK));
  zassert_equal(wl_poll(&f->ctx, 105U, &event), WL_ERR_NO_DATA);
  zassert_equal(f->calls, 3U);
  zassert_equal(encode_calls, 1U);
  zassert_equal(f->length, length);
  zassert_mem_equal(f->sent, image, length);
  zassert_equal(wl_tx_consumed(&f->ctx, token, WL_OK), WL_ERR_NOT_FOUND);
  zassert_ok(wl_tx_cancel(&f->ctx, handle));
  zassert_ok(wl_tx_consumed(&f->ctx, f->token, WL_OK));
  zassert_ok(wl_tx_drained(&f->ctx, f->token, WL_OK));
  zassert_ok(wl_tx_take(&f->ctx, handle, &(wl_tx_result_t){0}));
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
            size_t expected_calls = mode == FRAMING_BUSY3 || mode == FRAMING_RETRY3 ? 4U :
                mode == FRAMING_SEND || mode == FRAMING_CLAIM ? 1U : 0U;
            zassert_equal(f.calls - before_calls, expected_calls);
            zassert_equal(encode_calls - before_encode, mode < FRAMING_RAW_DECODE ? 1U : 0U);
          }
        }
    }
}

ZTEST(wirelink_framing_workload, test_bad_frames_recovery_partial_ring_and_leases) {
  fixture_t *f = &peers[0];
  init(f, config(0, 1, 1U));
  static uint8_t payload[512], raw[WL_FRAME_MAX_RAW_LEN], encoded[WL_FRAME_MAX_COBS_LEN];
  for (size_t b = 0U; b < sizeof(payload); ++b) payload[b] = (uint8_t)b;
  wl_wire_packet_t p = {.type = WL_PACKET_DATA, .flags = WL_PACKET_FLAG_RELIABLE,
      .message_id = 21U, .session_id = 42U, .sequence = 1U, .integrity = WL_INTEGRITY_CRC16,
      .payload = payload, .payload_len = sizeof(payload)};
  size_t raw_length = raw_reference(&p, raw);
  raw[raw_length - 1U] ^= 1U;
  size_t length = envelop(raw, raw_length, 0, encoded);
  zassert_ok(wl_feed_unit(&f->ctx, encoded, length));
  wl_event_t event;
  zassert_equal(wl_poll(&f->ctx, 0U, &event), WL_ERR_NO_DATA);
  zassert_equal(f->calls, 0U); /* No ACK on a bad reliable frame. */
  raw[1] = 1U;
  length = envelop(raw, raw_length, 0, encoded);
  zassert_ok(wl_feed_unit(&f->ctx, encoded, length));
  zassert_equal(wl_poll(&f->ctx, 0U, &event), WL_ERR_NO_DATA);
  raw[0] |= 3U;
  length = envelop(raw, raw_length, 0, encoded);
  zassert_ok(wl_feed_unit(&f->ctx, encoded, length));
  zassert_equal(wl_poll(&f->ctx, 0U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_feed_unit(&f->ctx, (const uint8_t[]){0U, 1U, 0U, 2U, 0U}, 5U));
  zassert_equal(wl_poll(&f->ctx, 0U, &event), WL_ERR_NO_DATA);
  wl_rx_counters_t counters;
  zassert_ok(wl_rx_get_counters(&f->ctx, &counters));
  zassert_equal(counters.bad_integrity, 1U);
  zassert_equal(counters.malformed, 2U);
  zassert_equal(counters.unsupported, 1U);
  for (unsigned round = 0U; round < 64U; ++round) {
    p.flags = 0U;
    p.payload_len = round % 2U != 0U ? 253U : sizeof(payload);
    raw_length = raw_reference(&p, raw);
    length = envelop(raw, raw_length, 0, encoded);
    for (size_t offset = 0U; offset < length;) {
      size_t chunk = length - offset > 17U ? 17U : length - offset;
      size_t accepted;
      zassert_ok(wl_feed_bytes(&f->ctx, encoded + offset, chunk, &accepted));
      zassert_equal(accepted, chunk);
      offset += chunk;
      if (offset != length) zassert_equal(wl_poll(&f->ctx, 0U, &event), WL_ERR_NO_DATA);
    }
    zassert_ok(wl_poll(&f->ctx, 0U, &event));
    zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
    zassert_equal(event.payload_len, p.payload_len);
    zassert_mem_equal(event.payload, payload, p.payload_len);
    zassert_equal(wl_poll(&f->ctx, 0U, &(wl_event_t){0}), WL_ERR_NO_DATA);
    zassert_mem_equal(event.payload, payload, p.payload_len);
    wl_event_release(&f->ctx, &event);
  }
  zassert_ok(wl_rx_get_counters(&f->ctx, &counters));
  zassert_equal(counters.overflow, 0U);
  zassert_equal(f->calls, 0U);
}

ZTEST_SUITE(wirelink_framing_workload, NULL, NULL, NULL, NULL, NULL);
