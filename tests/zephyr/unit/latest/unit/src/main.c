/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/latest.h"

struct test_value {
  uint32_t sequence;
  uint32_t inverse;
  uint64_t lanes[3];
};

struct fixture {
  wl_latest_t mailbox;
  struct test_value slots[WL_LATEST_SLOT_COUNT];
};

static struct fixture fixture;

void wl_latest_test_seed_saturating_counters(wl_latest_t *mailbox,
                                             const wl_latest_stats_t *stats);

static struct fixture *fixture_init(void) {
  const wl_latest_config_t config = {
      .value_size = sizeof(fixture.slots[0]),
      .value_alignment = _Alignof(struct test_value),
  };
  const wl_latest_storage_t storage = {
      .data = fixture.slots,
      .size = sizeof(fixture.slots),
  };

  memset(&fixture, 0, sizeof(fixture));
  zassert_ok(wl_latest_init(&fixture.mailbox, &config, &storage));
  return &fixture;
}

static void fill_value(struct test_value *value, uint32_t sequence) {
  size_t index;

  value->sequence = sequence;
  value->inverse = ~sequence;
  for (index = 0U; index < ARRAY_SIZE(value->lanes); index++) {
    value->lanes[index] = (UINT64_C(0x9e3779b97f4a7c15) * sequence) ^
                          (UINT64_C(0xd1b54a32d192ed03) * (index + 1U));
  }
}

static void expect_value(const struct test_value *value, uint32_t sequence) {
  struct test_value expected;

  fill_value(&expected, sequence);
  zassert_mem_equal(value, &expected, sizeof(expected));
}

static void publish_value(struct fixture *test, uint32_t sequence) {
  wl_latest_write_claim_t claim;

  zassert_ok(wl_latest_write_claim(&test->mailbox, &claim));
  zassert_equal(claim.value_size, sizeof(struct test_value));
  fill_value(claim.value, sequence);
  zassert_ok(wl_latest_write_publish(&test->mailbox, &claim));
}

ZTEST(wirelink_latest_unit, test_requirements_and_init_validation) {
  wl_latest_requirements_t requirements = {1U, 2U, 3U};
  wl_latest_config_t config = {.value_size = 3U, .value_alignment = 4U};
  wl_latest_storage_t storage;
  wl_latest_t mailbox = {0};
  _Alignas(8) uint8_t bytes[16];
  _Alignas(32) uint8_t over_aligned[96];

  zassert_equal(wl_latest_requirements(NULL, &requirements),
                WL_ERR_INVALID_ARG);
  zassert_equal(requirements.storage_size, 0U);
  zassert_equal(requirements.slot_stride, 0U);
  zassert_equal(requirements.slot_count, 0U);
  zassert_equal(wl_latest_requirements(&config, NULL), WL_ERR_INVALID_ARG);

  config.value_size = 0U;
  zassert_equal(wl_latest_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config = (wl_latest_config_t){.value_size = 3U, .value_alignment = 3U};
  zassert_equal(wl_latest_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config = (wl_latest_config_t){.value_size = SIZE_MAX, .value_alignment = 2U};
  zassert_equal(wl_latest_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);

  config = (wl_latest_config_t){.value_size = 3U, .value_alignment = 4U};
  zassert_ok(wl_latest_requirements(&config, &requirements));
  zassert_equal(requirements.slot_stride, 4U);
  zassert_equal(requirements.storage_size, 12U);
  zassert_equal(requirements.slot_count, WL_LATEST_SLOT_COUNT);

  storage = (wl_latest_storage_t){bytes, requirements.storage_size - 1U};
  zassert_equal(wl_latest_init(&mailbox, &config, &storage),
                WL_ERR_BUF_TOO_SMALL);
  storage = (wl_latest_storage_t){bytes + 1U, sizeof(bytes) - 1U};
  zassert_equal(wl_latest_init(&mailbox, &config, &storage),
                WL_ERR_INVALID_ARG);
  storage = (wl_latest_storage_t){bytes, sizeof(bytes)};
  zassert_ok(wl_latest_init(&mailbox, &config, &storage));

  config = (wl_latest_config_t){.value_size = 17U, .value_alignment = 32U};
  zassert_ok(wl_latest_requirements(&config, &requirements));
  zassert_equal(requirements.slot_stride, 32U);
  zassert_equal(requirements.storage_size, sizeof(over_aligned));
  storage = (wl_latest_storage_t){over_aligned, sizeof(over_aligned)};
  zassert_ok(wl_latest_init(&mailbox, &config, &storage));

  config = (wl_latest_config_t){.value_size = 4U, .value_alignment = 4U};
  storage = (wl_latest_storage_t){&mailbox, sizeof(mailbox)};
  zassert_equal(wl_latest_init(&mailbox, &config, &storage),
                WL_ERR_INVALID_ARG);

  zassert_equal(wl_latest_init(NULL, &config, &storage), WL_ERR_INVALID_ARG);
  zassert_equal(wl_latest_init(&mailbox, &config, NULL), WL_ERR_INVALID_ARG);
}

ZTEST(wirelink_latest_unit, test_uninitialized_and_null_arguments) {
  wl_latest_t mailbox = {0};
  wl_latest_write_claim_t claim = {0};
  wl_latest_view_t view = {0};
  wl_latest_stats_t stats = {0};

  zassert_equal(wl_latest_write_claim(&mailbox, &claim),
                WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_latest_write_publish(&mailbox, &claim),
                WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_latest_write_abort(&mailbox, &claim),
                WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_latest_read_acquire(&mailbox, &view),
                WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_latest_read_release(&mailbox, &view),
                WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_latest_reset(&mailbox), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_latest_get_stats(&mailbox, &stats), WL_ERR_NOT_INITIALIZED);

  zassert_equal(wl_latest_write_claim(NULL, &claim), WL_ERR_INVALID_ARG);
  zassert_equal(wl_latest_read_acquire(NULL, &view), WL_ERR_INVALID_ARG);
  zassert_equal(wl_latest_get_stats(NULL, &stats), WL_ERR_INVALID_ARG);
}

ZTEST(wirelink_latest_unit, test_first_read_repeat_and_token_validation) {
  struct fixture *test = fixture_init();
  wl_latest_write_claim_t claim;
  wl_latest_write_claim_t stale_claim;
  wl_latest_view_t view;
  wl_latest_view_t stale_view;
  wl_latest_stats_t stats;

  memset(&view, 0xa5, sizeof(view));
  zassert_equal(wl_latest_read_acquire(&test->mailbox, &view), WL_ERR_NO_DATA);
  zassert_is_null(view.value);
  zassert_equal(view.value_size, 0U);

  zassert_ok(wl_latest_write_claim(&test->mailbox, &claim));
  stale_claim = claim;
  zassert_equal(wl_latest_write_claim(&test->mailbox, &claim), WL_ERR_BUSY);
  zassert_is_null(claim.value);
  fill_value(stale_claim.value, 11U);
  zassert_ok(wl_latest_write_publish(&test->mailbox, &stale_claim));
  zassert_equal(wl_latest_write_publish(&test->mailbox, &stale_claim),
                WL_ERR_INVALID_STATE);

  zassert_ok(wl_latest_read_acquire(&test->mailbox, &view));
  stale_view = view;
  expect_value(view.value, 11U);
  zassert_equal(view.generation, 1U);
  zassert_equal(wl_latest_read_acquire(&test->mailbox, &view), WL_ERR_BUSY);
  zassert_is_null(view.value);
  zassert_ok(wl_latest_read_release(&test->mailbox, &stale_view));
  zassert_equal(wl_latest_read_release(&test->mailbox, &stale_view),
                WL_ERR_INVALID_STATE);
  zassert_equal(wl_latest_read_acquire(&test->mailbox, &view), WL_ERR_NO_DATA);

  zassert_ok(wl_latest_get_stats(&test->mailbox, &stats));
  zassert_equal(stats.generation, 1U);
  zassert_equal(stats.publishes, 1U);
  zassert_equal(stats.reads, 1U);
  zassert_equal(stats.coalesced, 0U);
  zassert_equal(stats.empty_reads, 2U);
  zassert_equal(stats.errors, 4U);
}

ZTEST(wirelink_latest_unit, test_coalescing_and_borrow_lifetime) {
  struct fixture *test = fixture_init();
  wl_latest_view_t view;
  wl_latest_stats_t stats;

  publish_value(test, 1U);
  publish_value(test, 2U);
  publish_value(test, 3U);

  zassert_ok(wl_latest_read_acquire(&test->mailbox, &view));
  expect_value(view.value, 3U);
  zassert_equal(view.generation, 3U);

  publish_value(test, 4U);
  publish_value(test, 5U);
  /* A borrowed front is immutable while both remaining slots rotate. */
  expect_value(view.value, 3U);
  zassert_ok(wl_latest_read_release(&test->mailbox, &view));

  zassert_ok(wl_latest_read_acquire(&test->mailbox, &view));
  expect_value(view.value, 5U);
  zassert_equal(view.generation, 5U);
  zassert_ok(wl_latest_read_release(&test->mailbox, &view));

  zassert_ok(wl_latest_get_stats(&test->mailbox, &stats));
  zassert_equal(stats.publishes, 5U);
  zassert_equal(stats.reads, 2U);
  zassert_equal(stats.coalesced, 3U);
}

ZTEST(wirelink_latest_unit, test_abort_and_reset_semantics) {
  struct fixture *test = fixture_init();
  wl_latest_write_claim_t claim;
  wl_latest_view_t view;
  wl_latest_stats_t stats;

  zassert_ok(wl_latest_write_claim(&test->mailbox, &claim));
  fill_value(claim.value, 1U);
  zassert_equal(wl_latest_reset(&test->mailbox), WL_ERR_BUSY);
  zassert_ok(wl_latest_write_abort(&test->mailbox, &claim));
  zassert_equal(wl_latest_read_acquire(&test->mailbox, &view), WL_ERR_NO_DATA);

  publish_value(test, 2U);
  zassert_ok(wl_latest_read_acquire(&test->mailbox, &view));
  zassert_equal(wl_latest_reset(&test->mailbox), WL_ERR_BUSY);
  zassert_ok(wl_latest_read_release(&test->mailbox, &view));
  publish_value(test, 3U);
  zassert_ok(wl_latest_reset(&test->mailbox));
  zassert_equal(wl_latest_read_acquire(&test->mailbox, &view), WL_ERR_NO_DATA);

  publish_value(test, 4U);
  zassert_ok(wl_latest_read_acquire(&test->mailbox, &view));
  expect_value(view.value, 4U);
  zassert_equal(view.generation, 3U);
  zassert_ok(wl_latest_read_release(&test->mailbox, &view));

  zassert_ok(wl_latest_get_stats(&test->mailbox, &stats));
  zassert_equal(stats.generation, 3U);
  zassert_equal(stats.publishes, 3U);
  zassert_equal(stats.reads, 2U);
  zassert_equal(stats.resets, 1U);
  zassert_equal(stats.errors, 2U);
}

ZTEST(wirelink_latest_unit, test_generation_wrap_is_pure_modulo) {
  wl_latest_t mailbox;
  struct test_value slots[WL_LATEST_SLOT_COUNT];
  const wl_latest_config_t config = {
      .value_size = sizeof(slots[0]),
      .value_alignment = _Alignof(struct test_value),
      .initial_generation = UINT32_MAX - 1U,
  };
  const wl_latest_storage_t storage = {slots, sizeof(slots)};
  wl_latest_stats_t stats;
  wl_latest_write_claim_t claim;
  wl_latest_view_t view;

  zassert_ok(wl_latest_init(&mailbox, &config, &storage));
  zassert_ok(wl_latest_write_claim(&mailbox, &claim));
  fill_value(claim.value, 1U);
  zassert_ok(wl_latest_write_publish(&mailbox, &claim));
  zassert_ok(wl_latest_read_acquire(&mailbox, &view));
  zassert_equal(view.generation, UINT32_MAX);
  zassert_ok(wl_latest_read_release(&mailbox, &view));

  zassert_ok(wl_latest_write_claim(&mailbox, &claim));
  fill_value(claim.value, 2U);
  zassert_ok(wl_latest_write_publish(&mailbox, &claim));
  zassert_ok(wl_latest_read_acquire(&mailbox, &view));
  zassert_equal(view.generation, 0U);
  expect_value(view.value, 2U);
  zassert_ok(wl_latest_read_release(&mailbox, &view));

  zassert_ok(wl_latest_get_stats(&mailbox, &stats));
  zassert_equal(stats.generation, 0U);
  zassert_equal(stats.publishes, 2U);
  zassert_equal(stats.reads, 2U);
}

ZTEST(wirelink_latest_unit, test_observability_counters_saturate) {
  struct fixture *test = fixture_init();
  const wl_latest_stats_t seed = {
      .publishes = UINT32_MAX,
      .reads = UINT32_MAX,
      .coalesced = UINT32_MAX,
      .empty_reads = UINT32_MAX,
      .resets = UINT32_MAX,
      .errors = UINT32_MAX,
  };
  wl_latest_stats_t stats;
  wl_latest_view_t active_view;
  wl_latest_view_t view;

  wl_latest_test_seed_saturating_counters(&test->mailbox, &seed);
  zassert_equal(wl_latest_read_acquire(&test->mailbox, &view), WL_ERR_NO_DATA);
  publish_value(test, 1U);
  publish_value(test, 2U);
  zassert_ok(wl_latest_read_acquire(&test->mailbox, &view));
  active_view = view;
  zassert_equal(wl_latest_read_acquire(&test->mailbox, &view), WL_ERR_BUSY);
  zassert_ok(wl_latest_read_release(&test->mailbox, &active_view));
  zassert_ok(wl_latest_reset(&test->mailbox));

  zassert_ok(wl_latest_get_stats(&test->mailbox, &stats));
  zassert_equal(stats.publishes, UINT32_MAX);
  zassert_equal(stats.reads, UINT32_MAX);
  zassert_equal(stats.coalesced, UINT32_MAX);
  zassert_equal(stats.empty_reads, UINT32_MAX);
  zassert_equal(stats.resets, UINT32_MAX);
  zassert_equal(stats.errors, UINT32_MAX);
}

ZTEST_SUITE(wirelink_latest_unit, NULL, NULL, NULL, NULL, NULL);
