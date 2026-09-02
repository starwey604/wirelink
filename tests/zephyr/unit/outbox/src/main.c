/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/outbox.h"

struct fixture {
  wl_outbox_t outbox;
  wl_outbox_slot_t slots[2];
  uint8_t payloads[2][8];
};

static struct fixture state;

static void init(void) {
  const wl_outbox_config_t config = {
      .slots = state.slots,
      .slot_count = ARRAY_SIZE(state.slots),
      .payload_storage = &state.payloads[0][0],
      .payload_storage_size = sizeof(state.payloads),
      .payload_capacity_per_slot = sizeof(state.payloads[0]),
      .initial_generation = 7U,
  };
  memset(&state, 0, sizeof(state));
  zassert_equal(wl_outbox_init(&state.outbox, &config), WL_OK);
}

ZTEST(wirelink_outbox, test_latest_lanes_coalesce_and_preserve_newer) {
  const uint8_t first[] = {1U, 2U};
  const uint8_t newer[] = {3U, 4U, 5U};
  uint8_t copied[8];
  uint8_t coalesced = 9U;
  wl_outbox_item_t item;
  wl_outbox_stats_t stats;

  init();
  zassert_equal(wl_outbox_submit_latest(&state.outbox, 10U, first,
                                        sizeof(first), &coalesced),
                WL_OK);
  zassert_equal(coalesced, 0U);
  zassert_equal(wl_outbox_acquire_copy(&state.outbox, copied, sizeof(copied),
                                       &item),
                WL_OK);
  zassert_equal(item.message_id, 10U);
  zassert_mem_equal(copied, first, sizeof(first));

  zassert_equal(wl_outbox_submit_latest(&state.outbox, 10U, newer,
                                        sizeof(newer), &coalesced),
                WL_OK);
  zassert_equal(coalesced, 1U);
  zassert_equal(wl_outbox_complete(&state.outbox, &item,
                                   WL_OUTBOX_ACCEPTED),
                WL_OK);
  zassert_equal(wl_outbox_acquire_copy(&state.outbox, copied, sizeof(copied),
                                       &item),
                WL_OK);
  zassert_equal(item.payload_length, sizeof(newer));
  zassert_mem_equal(copied, newer, sizeof(newer));
  zassert_equal(wl_outbox_complete(&state.outbox, &item,
                                   WL_OUTBOX_ACCEPTED),
                WL_OK);
  zassert_equal(wl_outbox_get_stats(&state.outbox, &stats), WL_OK);
  zassert_equal(stats.submitted, 2U);
  zassert_equal(stats.coalesced, 1U);
  zassert_equal(stats.superseded, 1U);
  zassert_equal(stats.accepted, 2U);
  zassert_equal(stats.depth, 0U);
}

ZTEST(wirelink_outbox, test_deferred_retry_queue_full_and_reset) {
  const uint8_t value = 0xa5U;
  uint8_t copied[8];
  wl_outbox_item_t item;
  wl_outbox_stats_t stats;
  uint16_t discarded = 0U;

  init();
  zassert_equal(wl_outbox_submit_latest(&state.outbox, 1U, &value, 1U, NULL),
                WL_OK);
  zassert_equal(wl_outbox_submit_latest(&state.outbox, 2U, &value, 1U, NULL),
                WL_OK);
  zassert_equal(wl_outbox_submit_latest(&state.outbox, 3U, &value, 1U, NULL),
                WL_ERR_QUEUE_FULL);
  zassert_equal(wl_outbox_acquire_copy(&state.outbox, copied, sizeof(copied),
                                       &item),
                WL_OK);
  zassert_equal(wl_outbox_reset(&state.outbox, &discarded), WL_ERR_BUSY);
  zassert_equal(wl_outbox_complete(&state.outbox, &item,
                                   WL_OUTBOX_DEFERRED),
                WL_OK);
  zassert_equal(wl_outbox_acquire_copy(&state.outbox, copied, sizeof(copied),
                                       &item),
                WL_OK);
  zassert_equal(wl_outbox_complete(&state.outbox, &item,
                                   WL_OUTBOX_REJECTED),
                WL_OK);
  zassert_equal(wl_outbox_reset(&state.outbox, &discarded), WL_OK);
  zassert_equal(discarded, 1U);
  zassert_equal(wl_outbox_get_stats(&state.outbox, &stats), WL_OK);
  zassert_equal(stats.queue_full, 1U);
  zassert_equal(stats.deferred, 1U);
  zassert_equal(stats.rejected, 1U);
  zassert_equal(stats.resets, 1U);
  zassert_equal(stats.depth, 0U);
}

ZTEST(wirelink_outbox, test_validation_and_acquire_lifecycle) {
  const uint8_t too_large[9] = {0U};
  uint8_t copied[8];
  wl_outbox_item_t item;

  init();
  zassert_equal(wl_outbox_submit_latest(&state.outbox, 0U, copied, 1U, NULL),
                WL_ERR_INVALID_ARG);
  zassert_equal(wl_outbox_submit_latest(&state.outbox, 1U, too_large,
                                        sizeof(too_large), NULL),
                WL_ERR_INVALID_ARG);
  zassert_equal(wl_outbox_acquire_copy(&state.outbox, copied, sizeof(copied),
                                       &item),
                WL_ERR_NO_DATA);
  zassert_equal(wl_outbox_submit_latest(&state.outbox, 1U, NULL, 0U, NULL),
                WL_OK);
  zassert_equal(wl_outbox_acquire_copy(&state.outbox, copied, sizeof(copied),
                                       &item),
                WL_OK);
  zassert_equal(wl_outbox_acquire_copy(&state.outbox, copied, sizeof(copied),
                                       &item),
                WL_ERR_BUSY);
  ++item.token;
  zassert_equal(wl_outbox_complete(&state.outbox, &item,
                                   WL_OUTBOX_ACCEPTED),
                WL_ERR_INVALID_STATE);
}

ZTEST_SUITE(wirelink_outbox, NULL, NULL, NULL, NULL, NULL);
