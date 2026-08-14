/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "rx_ring.h"
#include "wirelink/types.h"

ZTEST(rx_ring_lwrb, test_storage_size_accounts_for_sentinel) {
  zassert_equal(wl_rx_ring_storage_size(8U), 9U);
  zassert_equal(wl_rx_ring_storage_size(0U), 0U);
}

ZTEST(rx_ring_lwrb, test_exact_usable_capacity_and_reservation_contract) {
  wl_rx_ring_state_t state = {0};
  uint8_t memory[9] = {0};
  wl_span_t span = {0};
  const uint8_t input[] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};

  zassert_equal(wl_rx_ring_init(&state, memory, 1U), WL_ERR_INVALID_ARG);
  zassert_ok(wl_rx_ring_init(&state, memory, sizeof(memory)));
  zassert_ok(wl_rx_ring_producer_reserve(&state, &span));
  zassert_equal(span.length, sizeof(input));
  memcpy(span.data, input, sizeof(input));
  zassert_equal(wl_rx_ring_producer_reserve(&state, &span),
                WL_ERR_INVALID_STATE);
  zassert_equal(wl_rx_ring_producer_commit(&state, sizeof(input) + 1U),
                WL_ERR_INVALID_ARG);
  zassert_ok(wl_rx_ring_producer_commit(&state, sizeof(input)));
  zassert_equal(wl_rx_ring_readable(&state), sizeof(input));

  zassert_ok(wl_rx_ring_producer_reserve(&state, &span));
  zassert_equal(span.length, 0U);
  zassert_ok(wl_rx_ring_producer_commit(&state, 0U));
  zassert_equal(wl_rx_ring_producer_commit(&state, 0U), WL_ERR_INVALID_STATE);
}

ZTEST(rx_ring_lwrb, test_find_copy_consume_across_wrap) {
  wl_rx_ring_state_t state = {0};
  uint8_t memory[9] = {0};
  wl_span_t span = {0};
  uint8_t copied[6] = {0};
  const uint8_t first[] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
  const uint8_t expected[] = {6U, 7U, 8U, 9U, 10U, 11U};
  size_t found = 0U;

  zassert_ok(wl_rx_ring_init(&state, memory, sizeof(memory)));
  zassert_ok(wl_rx_ring_producer_reserve(&state, &span));
  memcpy(span.data, first, sizeof(first));
  zassert_ok(wl_rx_ring_producer_commit(&state, sizeof(first)));
  zassert_ok(wl_rx_ring_consumer_consume(&state, 6U));

  zassert_ok(wl_rx_ring_producer_reserve(&state, &span));
  zassert_equal(span.length, 1U);
  span.data[0] = 8U;
  zassert_ok(wl_rx_ring_producer_commit(&state, 1U));
  zassert_ok(wl_rx_ring_producer_reserve(&state, &span));
  zassert_true(span.length >= 3U);
  span.data[0] = 9U;
  span.data[1] = 10U;
  span.data[2] = 11U;
  zassert_ok(wl_rx_ring_producer_commit(&state, 3U));

  span = wl_rx_ring_consumer_peek(&state);
  zassert_equal(span.length, 3U);
  zassert_ok(wl_rx_ring_consumer_find(&state, 10U, &found));
  zassert_equal(found, 4U);
  zassert_equal(wl_rx_ring_consumer_find(&state, 42U, &found), WL_ERR_NO_DATA);
  zassert_ok(wl_rx_ring_consumer_copy(&state, 0U, copied, sizeof(copied)));
  zassert_mem_equal(copied, expected, sizeof(expected));
  zassert_ok(wl_rx_ring_consumer_copy(&state, sizeof(expected), NULL, 0U));
  zassert_equal(wl_rx_ring_consumer_copy(&state, 1U, copied, sizeof(copied)),
                WL_ERR_NO_DATA);
  zassert_equal(wl_rx_ring_consumer_consume(&state, sizeof(expected) + 1U),
                WL_ERR_NO_DATA);
  zassert_ok(wl_rx_ring_consumer_consume(&state, sizeof(expected)));
  zassert_equal(wl_rx_ring_readable(&state), 0U);
}

ZTEST_SUITE(rx_ring_lwrb, NULL, NULL, NULL, NULL, NULL);
