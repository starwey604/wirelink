/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/cobs.h"
#include "wirelink/frame.h"
#include "wirelink/wirelink.h"

#define TEST_MAX_PAYLOAD 64U
#define TEST_RX_STORAGE 128U
#define TEST_TX_STORAGE 128U

struct sink_capture {
  size_t calls;
};

struct rx_fixture {
  wl_ctx_t ctx;
  wl_config_t config;
  wl_storage_requirements_t requirements;
  wl_storage_t storage;
  struct sink_capture sink;
  uint8_t tx_payload[TEST_MAX_PAYLOAD];
  uint8_t tx_unit[TEST_TX_STORAGE];
  uint8_t control_unit[TEST_TX_STORAGE];
  uint8_t rx_fifo[TEST_RX_STORAGE];
  uint8_t rx_fallback[TEST_RX_STORAGE];
};

static wl_sink_result_t capture_sink(void *user_data, wl_io_token_t token,
                                     const uint8_t *data, size_t len) {
  struct sink_capture *capture = user_data;

  (void)token;
  (void)data;
  (void)len;
  capture->calls++;
  return WL_SINK_SENT;
}

static void init_fixture(struct rx_fixture *fixture) {
  memset(fixture, 0, sizeof(*fixture));
  fixture->config = (wl_config_t){
      .max_payload_len = TEST_MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .integrity = WL_INTEGRITY_NONE,
      .session_id = UINT64_C(0x123456789ABCDEF0),
      .max_retries = 1U,
      .ack_timeout_ms = 20U,
  };
  zassert_ok(wl_config_requirements(&fixture->config, &fixture->requirements));
  zassert_true(fixture->requirements.tx_payload_size <=
               sizeof(fixture->tx_payload));
  zassert_true(fixture->requirements.tx_unit_size <= sizeof(fixture->tx_unit));
  zassert_true(fixture->requirements.control_unit_size <=
               sizeof(fixture->control_unit));
  zassert_true(fixture->requirements.rx_fifo_size <= sizeof(fixture->rx_fifo));
  zassert_true(fixture->requirements.rx_fallback_size <=
               sizeof(fixture->rx_fallback));

  fixture->storage = (wl_storage_t){
      .tx_payload = fixture->tx_payload,
      .tx_payload_size = fixture->requirements.tx_payload_size,
      .tx_unit = fixture->tx_unit,
      .tx_unit_size = fixture->requirements.tx_unit_size,
      .control_unit = fixture->control_unit,
      .control_unit_size = fixture->requirements.control_unit_size,
      .rx_fifo = fixture->rx_fifo,
      .rx_fifo_size = fixture->requirements.rx_fifo_size,
      .rx_fallback = fixture->rx_fallback,
      .rx_fallback_size = fixture->requirements.rx_fallback_size,
  };
  zassert_ok(wl_init(&fixture->ctx, &fixture->config, &fixture->storage));
  zassert_ok(wl_set_sink(&fixture->ctx, capture_sink, &fixture->sink));
}

static size_t encode_data(const struct rx_fixture *fixture, uint8_t flags,
                          uint16_t cmd_id, uint32_t sequence,
                          const uint8_t *payload, size_t payload_len,
                          uint8_t *output, size_t output_size) {
  wl_wire_packet_t packet = {
      .type = WL_PACKET_DATA,
      .integrity = fixture->config.integrity,
      .flags = flags,
      .cmd_id = cmd_id,
      .session_id = UINT64_C(0x0FEDCBA987654321),
      .sequence = sequence,
      .payload = payload,
      .payload_len = payload_len,
  };
  size_t output_len = 0U;

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM, output,
                             output_size, &output_len));
  return output_len;
}

static size_t usable_rx_capacity(const struct rx_fixture *fixture) {
  return wl_frame_encode_overhead(WL_ENVELOPE_COBS_STREAM,
                                  fixture->config.integrity) +
         fixture->config.max_payload_len;
}

static bool points_into(const uint8_t *pointer, const uint8_t *storage,
                        size_t storage_size) {
  uintptr_t address = (uintptr_t)pointer;
  uintptr_t begin = (uintptr_t)storage;

  return address >= begin && address < begin + storage_size;
}

ZTEST(wirelink_rx_spsc, test_feed_only_enqueues_until_poll) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  const uint8_t payload[] = {0x10U, 0x00U, 0x20U};
  size_t accepted = SIZE_MAX;
  size_t wire_len;
  wl_event_t event = {0};

  init_fixture(&fixture);
  wire_len = encode_data(&fixture, WL_PACKET_FLAG_RELIABLE, 0x102U, 7U, payload,
                         sizeof(payload), wire, sizeof(wire));

  zassert_ok(wl_feed_bytes(&fixture.ctx, wire, wire_len, &accepted));
  zassert_equal(accepted, wire_len);
  zassert_equal(fixture.sink.calls, 0U, "producer path must not emit an ACK");
  zassert_equal(fixture.ctx.has_event, 0U,
                "producer path must not parse or publish an event");
  zassert_equal(fixture.ctx.control_pending, 0U);

  zassert_ok(wl_poll(&fixture.ctx, 1U, &event));
  zassert_equal(event.type, WL_EVT_RELIABLE_RX);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  zassert_equal(fixture.sink.calls, 1U, "poll must submit the ACK");
  wl_event_release(&fixture.ctx, &event);
}

ZTEST(wirelink_rx_spsc, test_contiguous_payload_is_borrowed_from_ring) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  const uint8_t payload[] = {0x00U, 0x11U, 0x22U, 0x00U, 0x33U};
  size_t accepted = 0U;
  size_t wire_len;
  wl_event_t event = {0};

  init_fixture(&fixture);
  wire_len = encode_data(&fixture, 0U, 0x201U, 1U, payload, sizeof(payload),
                         wire, sizeof(wire));
  zassert_ok(wl_feed_bytes(&fixture.ctx, wire, wire_len, &accepted));
  memset(wire, 0xA5, wire_len);

  zassert_ok(wl_poll(&fixture.ctx, 2U, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  zassert_true(points_into(event.payload, fixture.rx_fifo,
                           fixture.storage.rx_fifo_size));
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  zassert_equal(fixture.ctx.rx_event_leased, 1U);
  wl_event_release(&fixture.ctx, &event);
  zassert_equal(fixture.ctx.rx_event_leased, 0U);
}

ZTEST(wirelink_rx_spsc, test_wrapped_frame_uses_fallback) {
  struct rx_fixture fixture;
  uint8_t first[TEST_RX_STORAGE];
  uint8_t wrapped[TEST_RX_STORAGE];
  uint8_t payload[TEST_MAX_PAYLOAD];
  size_t accepted = 0U;
  size_t first_len;
  size_t wrapped_len;
  size_t capacity;
  size_t prefix_len;
  wl_event_t event = {0};

  for (size_t i = 0U; i < sizeof(payload); ++i) {
    payload[i] = (uint8_t)(i * 3U);
  }
  init_fixture(&fixture);
  first_len =
      encode_data(&fixture, 0U, 0x301U, 1U, NULL, 0U, first, sizeof(first));
  wrapped_len = encode_data(&fixture, 0U, 0x302U, 2U, payload, sizeof(payload),
                            wrapped, sizeof(wrapped));
  capacity = usable_rx_capacity(&fixture);
  zassert_equal(wrapped_len, capacity,
                "maximum test frame must fill the usable ring");
  zassert_true(first_len < capacity);
  prefix_len = capacity - first_len;

  zassert_ok(wl_feed_bytes(&fixture.ctx, first, first_len, &accepted));
  zassert_ok(wl_feed_bytes(&fixture.ctx, wrapped, prefix_len, &accepted));
  zassert_ok(wl_poll(&fixture.ctx, 3U, &event));
  zassert_equal(event.cmd_id, 0x301U);
  wl_event_release(&fixture.ctx, &event);

  zassert_ok(wl_feed_bytes(&fixture.ctx, wrapped + prefix_len,
                           wrapped_len - prefix_len, &accepted));
  event = (wl_event_t){0};
  zassert_ok(wl_poll(&fixture.ctx, 4U, &event));
  zassert_equal(event.cmd_id, 0x302U);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  zassert_true(points_into(event.payload, fixture.rx_fallback,
                           fixture.storage.rx_fallback_size));
  zassert_false(points_into(event.payload, fixture.rx_fifo,
                            fixture.storage.rx_fifo_size));
  wl_event_release(&fixture.ctx, &event);
}

ZTEST(wirelink_rx_spsc, test_held_event_prevents_borrowed_bytes_overwrite) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  uint8_t filler[TEST_RX_STORAGE];
  const uint8_t payload[] = {0xA1U, 0x00U, 0xB2U, 0xC3U};
  size_t accepted = 0U;
  size_t wire_len;
  size_t capacity;
  wl_event_t event = {0};

  memset(filler, 0x5AU, sizeof(filler));
  init_fixture(&fixture);
  wire_len = encode_data(&fixture, 0U, 0x401U, 1U, payload, sizeof(payload),
                         wire, sizeof(wire));
  capacity = usable_rx_capacity(&fixture);
  zassert_ok(wl_feed_bytes(&fixture.ctx, wire, wire_len, &accepted));
  zassert_ok(wl_poll(&fixture.ctx, 5U, &event));
  zassert_true(points_into(event.payload, fixture.rx_fifo,
                           fixture.storage.rx_fifo_size));

  zassert_ok(
      wl_feed_bytes(&fixture.ctx, filler, capacity - wire_len, &accepted));
  zassert_equal(accepted, capacity - wire_len);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  zassert_equal(wl_feed_bytes(&fixture.ctx, filler, 1U, &accepted),
                WL_ERR_WOULD_BLOCK);
  zassert_equal(accepted, 0U);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&fixture.ctx, &event);
}

ZTEST(wirelink_rx_spsc, test_reserve_short_commit_and_double_reserve) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  const uint8_t payload[] = {0x12U, 0x00U, 0x34U};
  const size_t first_chunk = 5U;
  size_t accepted = 0U;
  size_t wire_len;
  wl_span_t reservation = {0};
  wl_span_t second = {0};
  wl_event_t event = {0};

  init_fixture(&fixture);
  wire_len = encode_data(&fixture, 0U, 0x501U, 1U, payload, sizeof(payload),
                         wire, sizeof(wire));
  zassert_ok(wl_rx_reserve(&fixture.ctx, &reservation));
  zassert_true(reservation.length >= first_chunk);
  memcpy(reservation.data, wire, first_chunk);
  zassert_equal(wl_rx_reserve(&fixture.ctx, &second), WL_ERR_INVALID_STATE);
  zassert_ok(wl_rx_commit(&fixture.ctx, first_chunk));
  zassert_equal(wl_rx_commit(&fixture.ctx, 0U), WL_ERR_INVALID_STATE);

  zassert_ok(wl_feed_bytes(&fixture.ctx, wire + first_chunk,
                           wire_len - first_chunk, &accepted));
  zassert_equal(accepted, wire_len - first_chunk);
  zassert_ok(wl_poll(&fixture.ctx, 6U, &event));
  zassert_equal(event.cmd_id, 0x501U);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&fixture.ctx, &event);
}

ZTEST(wirelink_rx_spsc, test_full_buffer_reports_partial_acceptance) {
  struct rx_fixture fixture;
  uint8_t input[TEST_RX_STORAGE + 8U];
  size_t accepted = SIZE_MAX;
  size_t capacity;

  memset(input, 0x7EU, sizeof(input));
  init_fixture(&fixture);
  capacity = usable_rx_capacity(&fixture);

  zassert_equal(wl_feed_bytes(&fixture.ctx, input, capacity + 3U, &accepted),
                WL_ERR_WOULD_BLOCK);
  zassert_equal(accepted, capacity);
  zassert_equal(wl_feed_bytes(&fixture.ctx, input, 1U, &accepted),
                WL_ERR_WOULD_BLOCK);
  zassert_equal(accepted, 0U);
  zassert_ok(wl_feed_recover_reset(&fixture.ctx));
  zassert_ok(wl_feed_bytes(&fixture.ctx, input, 1U, &accepted));
  zassert_equal(accepted, 1U);
}

ZTEST(wirelink_rx_spsc, test_cobs_decode_in_place) {
  const uint8_t original[] = {0x00U, 0x11U, 0x22U, 0x00U,
                              0x33U, 0x00U, 0x00U, 0x44U};
  uint8_t buffer[32];
  size_t encoded_len = 0U;
  size_t decoded_len = 0U;

  zassert_ok(wl_cobs_encode(original, sizeof(original), buffer, sizeof(buffer),
                            &encoded_len));
  zassert_ok(wl_cobs_decode_in_place(buffer, encoded_len, &decoded_len));
  zassert_equal(decoded_len, sizeof(original));
  zassert_mem_equal(buffer, original, sizeof(original));
}

ZTEST_SUITE(wirelink_rx_spsc, NULL, NULL, NULL, NULL, NULL);
