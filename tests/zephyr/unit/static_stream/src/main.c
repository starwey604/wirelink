/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/byte_fifo.h"
#include <zephyr/ztest.h>
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
ZTEST_SUITE(byte_fifo, NULL, NULL, NULL, NULL, NULL);

void wl_test_packet_stream(int);
ZTEST(static_stream, test_cobs_crc16_vectors) { wl_test_packet_stream(1); }
ZTEST(static_stream, test_length16_none_vectors) { wl_test_packet_stream(0); }
ZTEST_SUITE(static_stream, NULL, NULL, NULL, NULL, NULL);
