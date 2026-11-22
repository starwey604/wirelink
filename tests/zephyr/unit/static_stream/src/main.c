/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/byte_fifo.h"
#include "wirelink/node.h"
#include <zephyr/ztest.h>
#include <string.h>
ZTEST(byte_fifo, test_non_power_of_two_wrap_and_capacity) {
  uint8_t bytes[37];
  wl_byte_fifo_t fifo;
  zassert_ok(wl_byte_fifo_init(&fifo, (wl_span_t){bytes, sizeof(bytes)}, NULL, NULL));
  unsigned produced = 0U, consumed = 0U;
  for (unsigned pass = 0U; pass < 10000U; ++pass) {
    wl_span_t span;
    zassert_ok(wl_byte_fifo_reserve(&fifo, &span));
    for (size_t i = 0U; i < span.length; ++i) span.data[i] = (uint8_t)(produced++);
    zassert_equal(wl_byte_fifo_commit(&fifo, span.length + 1U), WL_ERR_INVALID_ARG);
    zassert_ok(wl_byte_fifo_commit(&fifo, span.length));
    span = wl_byte_fifo_peek(&fifo);
    zassert_equal(wl_byte_fifo_consume(&fifo, span.length + 1U), WL_ERR_INVALID_ARG);
    for (size_t i = 0U; i < span.length; ++i) zassert_equal(span.data[i], (uint8_t)(consumed++));
    zassert_ok(wl_byte_fifo_consume(&fifo, span.length));
  }
  zassert_equal(produced, consumed);
  wl_byte_fifo_note_gap(&fifo);
  zassert_true(wl_byte_fifo_take_gap(&fifo)); zassert_false(wl_byte_fifo_take_gap(&fifo));
}
ZTEST(byte_fifo, test_partial_commit_and_held_read_protect_published_bytes) {
  uint8_t memory[8]; wl_byte_fifo_t fifo;
  wl_span_t producer;
  zassert_ok(wl_byte_fifo_init(&fifo, (wl_span_t){memory, sizeof(memory)}, NULL, NULL));
  zassert_ok(wl_byte_fifo_reserve(&fifo, &producer));
  for (size_t i = 0; i < producer.length; ++i) producer.data[i] = (uint8_t)(10 + i);
  zassert_equal(wl_byte_fifo_commit(&fifo, producer.length + 1), WL_ERR_INVALID_ARG);
  zassert_false(wl_byte_fifo_readable(&fifo));
  zassert_ok(wl_byte_fifo_commit(&fifo, 3));
  zassert_ok(wl_byte_fifo_reserve(&fifo, &producer));
  producer.data[0] = 20; producer.data[1] = 21;
  zassert_ok(wl_byte_fifo_commit(&fifo, 2));
  const uint8_t expected[] = {10, 11, 12, 20, 21};
  wl_span_t held = wl_byte_fifo_peek(&fifo);
  zassert_equal(held.length, sizeof(expected)); zassert_mem_equal(held.data, expected, sizeof(expected));
  zassert_ok(wl_byte_fifo_reserve(&fifo, &producer));
  zassert_equal(producer.length, 3); memset(producer.data, 30, producer.length);
  zassert_ok(wl_byte_fifo_commit(&fifo, 3));
  zassert_equal(wl_byte_fifo_reserve(&fifo, &producer), WL_ERR_WOULD_BLOCK);
  zassert_mem_equal(held.data, expected, sizeof(expected));
  zassert_ok(wl_byte_fifo_consume(&fifo, held.length));
  held = wl_byte_fifo_peek(&fifo); zassert_equal(held.length, 3);
  for (size_t i = 0; i < held.length; ++i) zassert_equal(held.data[i], 30);
  zassert_ok(wl_byte_fifo_consume(&fifo, held.length));
  zassert_false(wl_byte_fifo_readable(&fifo));
}
ZTEST_SUITE(byte_fifo, NULL, NULL, NULL, NULL, NULL);

void wl_test_packet_stream(int);
ZTEST(static_stream, test_cobs_crc16_vectors) { wl_test_packet_stream(1); }
ZTEST(static_stream, test_length16_none_vectors) { wl_test_packet_stream(0); }
void wl_test_stream_rx_limit(int);
ZTEST(static_stream, test_rx_capacity_independent_of_tx_bound) {
  wl_test_stream_rx_limit(1); wl_test_stream_rx_limit(0);
}
ZTEST(static_stream, test_node_close_releases_partial_frame) {
  static const wl_node_stream_ops_t cobs = {
      wl_packet_stream_cobs_crc16_init, wl_packet_stream_cobs_crc16_submit,
      wl_packet_stream_cobs_crc16_feed, wl_packet_stream_abort_rx, wl_packet_stream_notify,
      WL_NODE_COBS_CRC16};
  static const wl_node_stream_ops_t length16 = {
      wl_packet_stream_length16_none_init, wl_packet_stream_length16_none_submit,
      wl_packet_stream_length16_none_feed, wl_packet_stream_abort_rx, wl_packet_stream_notify,
      WL_NODE_LENGTH16};
  static wl_node_t node;
  uint8_t slots[WL_PACKET_RX_SLOTS][32], tx[32];
  wl_packet_stream_t stream;
  wl_node_layout_t layout = {
      .rx_storage = slots[0], .rx_storage_size = sizeof(slots), .rx_frame_max = 16U,
      .tx_frame_max = 16U, .stream = &stream, .stream_tx_wire = {tx, sizeof(tx)}};
  for (unsigned i = 0; i < 2U; ++i) {
    layout.stream_ops = i == 0U ? &cobs : &length16;
    layout.rx_wire_max = layout.tx_wire_max = i == 0U ? wl_packet_stream_cobs_crc16_bound(16U)
                                                       : wl_packet_stream_length16_none_bound(16U);
    const uint8_t partial_cobs[] = {4, 1, 2}, partial_length16[] = {4, 0, 2};
    size_t consumed = 0U;
    zassert_ok(wl_node_bind_sync(&node, &layout, NULL));
    zassert_ok(wl_node_rx_feed(&node, i == 0U ? partial_cobs : partial_length16, 3U, &consumed));
    zassert_equal(consumed, 3U);
    zassert_true(stream.claimed);
    zassert_ok(wl_node_close(&node, 0U));
    zassert_false(stream.claimed);
    zassert_true(wl_node_quiescent(&node));
  }
}
ZTEST_SUITE(static_stream, NULL, NULL, NULL, NULL, NULL);
