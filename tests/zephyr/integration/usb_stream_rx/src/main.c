/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include "wirelink/detail/usb_stream_rx.h"
#include "wirelink/frame.h"
#include "context.h"
#include "rx_ring.h"

static wl_ctx_t link;
static wl_usb_stream_rx_t rx;
static uint8_t fifo[8192], payload[2048], tx[WL_FRAME_MAX_COBS_LEN];
static uint8_t control[WL_FRAME_MAX_COBS_LEN], fallback[WL_FRAME_MAX_COBS_LEN];
static uint8_t staging[512], wire[WL_FRAME_MAX_COBS_LEN];

static void setup(void *unused) {
  (void)unused;
  wl_config_t config = {.max_payload_len = 2048, .envelope = WL_ENVELOPE_COBS_STREAM,
    .integrity = WL_INTEGRITY_NONE, .session_id = 1, .ack_timeout_ms = 5};
  wl_storage_t storage = {.tx_payload = payload, .tx_payload_size = sizeof(payload),
    .tx_unit = tx, .tx_unit_size = sizeof(tx), .control_unit = control,
    .control_unit_size = sizeof(control), .rx_fifo = fifo, .rx_fifo_size = sizeof(fifo),
    .rx_fallback = fallback, .rx_fallback_size = sizeof(fallback)};
  zassert_ok(wl_init(&link, &config, &storage));
  wl_usb_stream_rx_init(&rx, &link, staging, sizeof(staging));
}

static void position_empty(size_t cursor) {
  wl_span_t span;
  wl_event_t event;
  zassert_ok(wl_rx_reserve(&link, &span));
  memset(span.data, 0, cursor);
  zassert_ok(wl_rx_commit(&link, cursor));
  zassert_equal(wl_poll(&link, 0, &event), WL_ERR_NO_DATA);
}

static void split_frame(size_t packet_size, size_t cursor) {
  uint8_t expected[2048];
  size_t length, sent = 0, staged = 0;
  wl_event_t event;
  for (size_t i = 0; i < sizeof(expected); ++i) expected[i] = (uint8_t)(i * 31U + 7U);
  wl_wire_packet_t packet = {.type = WL_PACKET_DATA, .integrity = WL_INTEGRITY_NONE,
    .message_id = 42, .payload = expected, .payload_len = sizeof(expected)};
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM, wire, sizeof(wire), &length));
  position_empty(cursor);
  while (sent < length) {
    zassert_ok(wl_usb_stream_rx_acquire(&rx, 512, packet_size));
    zassert_true(rx.active_span.length >= packet_size);
    zassert_equal(rx.active_span.length % packet_size, 0);
    size_t count = MIN(rx.active_span.length, length - sent);
    staged += rx.staged != 0;
    memcpy(rx.active_span.data, wire + sent, count);
    zassert_ok(wl_usb_stream_rx_complete(&rx, rx.token, rx.active_span.data, count));
    zassert_ok(wl_usb_stream_rx_drain(&rx));
    sent += count;
    if (sent < length) zassert_equal(wl_poll(&link, 0, &event), WL_ERR_NO_DATA);
  }
  zassert_true(staged > 0, "test must cross a short physical ring tail");
  zassert_ok(wl_poll(&link, 0, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  zassert_equal(event.payload_len, sizeof(expected));
  zassert_mem_equal(event.payload, expected, sizeof(expected));
  wl_event_release(&link, &event);
  wl_rx_counters_t counters;
  zassert_ok(wl_rx_get_counters(&link, &counters));
  zassert_equal(counters.overflow, 0);
  zassert_equal(counters.malformed, 0);
}

ZTEST(usb_stream_rx, test_full_speed_split_at_hil_cursor) { split_frame(64, 6369); }
ZTEST(usb_stream_rx, test_high_speed_split_at_hil_cursor) { split_frame(512, 6369); }
ZTEST(usb_stream_rx, test_one_byte_tail) { split_frame(64, 8192 - 1025); }

ZTEST(usb_stream_rx, test_staged_backpressure_preserves_bytes) {
  wl_span_t span;
  zassert_ok(wl_rx_reserve(&link, &span));
  memset(span.data, 0x55, sizeof(fifo) - 3);
  zassert_ok(wl_rx_commit(&link, sizeof(fifo) - 3));
  zassert_ok(wl_usb_stream_rx_acquire(&rx, 512, 64));
  zassert_true(rx.staged);
  memset(rx.active_span.data, 0x66, 64);
  zassert_ok(wl_usb_stream_rx_complete(&rx, rx.token, rx.active_span.data, 64));
  zassert_equal(wl_usb_stream_rx_drain(&rx), WL_ERR_WOULD_BLOCK);
  zassert_equal(rx.pending_offset, 3);
  zassert_equal(wl_usb_stream_rx_acquire(&rx, 512, 64), WL_ERR_WOULD_BLOCK);
  zassert_false(wl_rx_ring_consumer_overflow_pending(&wl_ctx_impl(&link)->rx_ring));
  zassert_ok(wl_rx_ring_consumer_consume(&wl_ctx_impl(&link)->rx_ring, sizeof(fifo) - 3));
  zassert_ok(wl_usb_stream_rx_drain(&rx));
  uint8_t actual[64];
  zassert_ok(wl_rx_ring_consumer_copy(&wl_ctx_impl(&link)->rx_ring, 0, actual, sizeof(actual)));
  for (size_t i = 0; i < sizeof(actual); ++i) zassert_equal(actual[i], 0x66);
  zassert_false(wl_rx_ring_consumer_overflow_pending(&wl_ctx_impl(&link)->rx_ring));
}

ZTEST(usb_stream_rx, test_zero_length_completion_and_invalid_token) {
  zassert_ok(wl_usb_stream_rx_acquire(&rx, 512, 64));
  zassert_equal(wl_usb_stream_rx_acquire(&rx, 512, 64), WL_ERR_INVALID_STATE);
  zassert_ok(wl_usb_stream_rx_complete(&rx, rx.token, rx.active_span.data, 0));
  zassert_ok(wl_usb_stream_rx_acquire(&rx, 512, 64));
  zassert_equal(wl_usb_stream_rx_complete(&rx, rx.token + 1, rx.active_span.data, 1), WL_ERR_INVALID_ARG);
  zassert_false(rx.active);
  wl_event_t event;
  zassert_equal(wl_poll(&link, 0, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_usb_stream_rx_acquire(&rx, 512, 64));
  zassert_ok(wl_usb_stream_rx_complete(&rx, rx.token, rx.active_span.data, 0));
}

ZTEST_SUITE(usb_stream_rx, NULL, NULL, setup, NULL, NULL);
