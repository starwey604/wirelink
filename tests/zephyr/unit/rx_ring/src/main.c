/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "rx_ring.h"
#include "wirelink/types.h"

static void reserve_copy_commit(wl_rx_ring_state_t *ring, const uint8_t *data,
                                size_t length) {
  size_t copied = 0U;

  while (copied < length) {
    wl_span_t span = {0};
    size_t chunk;

    zassert_ok(wl_rx_ring_producer_reserve(ring, &span));
    zassert_true(span.length > 0U);
    chunk = length - copied;
    if (chunk > span.length) {
      chunk = span.length;
    }
    memcpy(span.data, data + copied, chunk);
    zassert_ok(wl_rx_ring_producer_commit(ring, chunk));
    copied += chunk;
  }
}

ZTEST(wirelink_rx_ring, test_wrap_preserves_logical_order)
{
  wl_rx_ring_state_t ring = {0};
  uint8_t memory[8] = {0};
  const uint8_t first[] = {1U, 2U, 3U, 4U, 5U, 6U};
  const uint8_t second[] = {7U, 8U, 9U, 10U, 11U};
  const uint8_t expected[] = {5U, 6U, 7U, 8U, 9U, 10U, 11U};
  uint8_t copied[sizeof(expected)] = {0};
  size_t found = 0U;
  wl_span_t span;

  zassert_ok(wl_rx_ring_init(&ring, memory, sizeof(memory)));
  reserve_copy_commit(&ring, first, sizeof(first));
  zassert_ok(wl_rx_ring_consumer_consume(&ring, 4U));
  reserve_copy_commit(&ring, second, sizeof(second));

  zassert_equal(wl_rx_ring_readable(&ring), sizeof(expected));
  span = wl_rx_ring_consumer_peek(&ring);
  zassert_equal(span.length, 4U);
  zassert_mem_equal(span.data, expected, span.length);
  zassert_ok(wl_rx_ring_consumer_copy(&ring, 0U, copied, sizeof(copied)));
  zassert_mem_equal(copied, expected, sizeof(expected));
  zassert_ok(wl_rx_ring_consumer_find(&ring, 10U, &found));
  zassert_equal(found, 5U);
}

ZTEST(wirelink_rx_ring, test_held_read_is_not_offered_to_producer)
{
  wl_rx_ring_state_t ring = {0};
  uint8_t memory[6] = {0};
  const uint8_t input[] = {1U, 2U, 3U, 4U};
  const uint8_t tail[] = {5U, 6U};
  wl_span_t read_span;
  wl_span_t write_span;

  zassert_ok(wl_rx_ring_init(&ring, memory, sizeof(memory)));
  reserve_copy_commit(&ring, input, sizeof(input));
  read_span = wl_rx_ring_consumer_peek(&ring);
  zassert_equal(read_span.length, sizeof(input));
  zassert_mem_equal(read_span.data, input, sizeof(input));

  zassert_ok(wl_rx_ring_producer_reserve(&ring, &write_span));
  zassert_equal(write_span.length, sizeof(tail));
  memcpy(write_span.data, tail, sizeof(tail));
  zassert_ok(wl_rx_ring_producer_commit(&ring, sizeof(tail)));
  zassert_mem_equal(read_span.data, input, sizeof(input));

  zassert_ok(wl_rx_ring_producer_reserve(&ring, &write_span));
  zassert_equal(write_span.length, 0U);
  zassert_ok(wl_rx_ring_producer_commit(&ring, 0U));
  zassert_mem_equal(read_span.data, input, sizeof(input));

  zassert_ok(wl_rx_ring_consumer_consume(&ring, sizeof(input)));
  zassert_ok(wl_rx_ring_producer_reserve(&ring, &write_span));
  zassert_equal(write_span.length, sizeof(input));
  zassert_ok(wl_rx_ring_producer_commit(&ring, 0U));
}

ZTEST(wirelink_rx_ring, test_partial_commit_publishes_only_committed_bytes)
{
  wl_rx_ring_state_t ring = {0};
  uint8_t memory[8] = {0};
  wl_span_t span;
  uint8_t copied[5] = {0};
  const uint8_t expected[] = {10U, 11U, 12U, 20U, 21U};

  zassert_ok(wl_rx_ring_init(&ring, memory, sizeof(memory)));
  zassert_ok(wl_rx_ring_producer_reserve(&ring, &span));
  zassert_equal(span.length, sizeof(memory));
  for (size_t i = 0U; i < span.length; ++i) {
    span.data[i] = (uint8_t)(10U + i);
  }
  zassert_equal(wl_rx_ring_producer_commit(&ring, span.length + 1U),
                WL_ERR_INVALID_ARG);
  zassert_equal(wl_rx_ring_readable(&ring), 0U);
  zassert_ok(wl_rx_ring_producer_commit(&ring, 3U));
  zassert_equal(wl_rx_ring_readable(&ring), 3U);

  zassert_ok(wl_rx_ring_producer_reserve(&ring, &span));
  zassert_equal(span.length, 5U);
  span.data[0] = 20U;
  span.data[1] = 21U;
  zassert_ok(wl_rx_ring_producer_commit(&ring, 2U));

  zassert_ok(wl_rx_ring_consumer_copy(&ring, 0U, copied, sizeof(copied)));
  zassert_mem_equal(copied, expected, sizeof(expected));
  zassert_ok(wl_rx_ring_consumer_consume(&ring, sizeof(copied)));
  zassert_equal(wl_rx_ring_readable(&ring), 0U);
}

ZTEST(wirelink_rx_ring, test_cursor_epoch_rollover_with_non_power_of_two_size)
{
  wl_rx_ring_state_t ring = {0};
  uint8_t memory[5] = {0};
  uint8_t copied[3] = {0};

  zassert_ok(wl_rx_ring_init(&ring, memory, sizeof(memory)));
  for (uint8_t round = 0U; round < 12U; ++round) {
    const uint8_t input[] = {round, (uint8_t)(round + 1U),
                             (uint8_t)(round + 2U)};

    reserve_copy_commit(&ring, input, sizeof(input));
    zassert_ok(wl_rx_ring_consumer_copy(&ring, 0U, copied, sizeof(copied)));
    zassert_mem_equal(copied, input, sizeof(input));
    zassert_ok(wl_rx_ring_consumer_consume(&ring, sizeof(input)));
    zassert_equal(wl_rx_ring_readable(&ring), 0U);
  }
}

ZTEST_SUITE(wirelink_rx_ring, NULL, NULL, NULL, NULL, NULL);
