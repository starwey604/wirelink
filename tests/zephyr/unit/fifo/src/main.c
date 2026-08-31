/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/fifo.h"

#define TEST_CAPACITY 3U

struct test_value {
  uint32_t sequence;
  uint32_t inverse;
  uint64_t lanes[3];
};

struct fixture {
  wl_fifo_t fifo;
  struct test_value slots[TEST_CAPACITY];
};

static struct fixture fixture;

void wl_fifo_test_seed_saturating_counters(wl_fifo_t *fifo,
                                           const wl_fifo_stats_t *stats);
void wl_fifo_test_seed_cursors(wl_fifo_t *fifo, uint32_t cursor);

static struct fixture *fixture_init(void) {
  const wl_fifo_config_t config = {
      .value_size = sizeof(fixture.slots[0]),
      .value_alignment = _Alignof(struct test_value),
      .capacity = ARRAY_SIZE(fixture.slots),
  };
  const wl_fifo_storage_t storage = {
      .data = fixture.slots,
      .size = sizeof(fixture.slots),
  };

  memset(&fixture, 0, sizeof(fixture));
  zassert_ok(wl_fifo_init(&fixture.fifo, &config, &storage));
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
  wl_fifo_write_claim_t claim;

  zassert_ok(wl_fifo_write_claim(&test->fifo, &claim));
  zassert_equal(claim.value_size, sizeof(struct test_value));
  fill_value(claim.value, sequence);
  zassert_ok(wl_fifo_write_publish(&test->fifo, &claim));
}

static void consume_value(struct fixture *test, uint32_t sequence) {
  wl_fifo_view_t view;

  zassert_ok(wl_fifo_read_acquire(&test->fifo, &view));
  zassert_equal(view.value_size, sizeof(struct test_value));
  expect_value(view.value, sequence);
  zassert_ok(wl_fifo_read_release(&test->fifo, &view));
}

ZTEST(wirelink_fifo_unit, test_requirements_and_init_validation) {
  wl_fifo_requirements_t requirements = {1U, 2U, 3U};
  wl_fifo_config_t config = {
      .value_size = 3U,
      .value_alignment = 4U,
      .capacity = 3U,
  };
  wl_fifo_storage_t storage;
  wl_fifo_t fifo = {0};
  _Alignas(8) uint8_t bytes[16];
  _Alignas(32) uint8_t over_aligned[128];

  zassert_equal(wl_fifo_requirements(NULL, &requirements), WL_ERR_INVALID_ARG);
  zassert_equal(requirements.storage_size, 0U);
  zassert_equal(requirements.slot_stride, 0U);
  zassert_equal(requirements.slot_count, 0U);
  zassert_equal(wl_fifo_requirements(&config, NULL), WL_ERR_INVALID_ARG);

  config.value_size = 0U;
  zassert_equal(wl_fifo_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config = (wl_fifo_config_t){
      .value_size = 3U,
      .value_alignment = 3U,
      .capacity = 3U,
  };
  zassert_equal(wl_fifo_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config = (wl_fifo_config_t){
      .value_size = 3U,
      .value_alignment = 4U,
      .capacity = 0U,
  };
  zassert_equal(wl_fifo_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config.capacity = UINT32_C(0x80000000);
  zassert_equal(wl_fifo_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config = (wl_fifo_config_t){
      .value_size = SIZE_MAX,
      .value_alignment = 2U,
      .capacity = 1U,
  };
  zassert_equal(wl_fifo_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config = (wl_fifo_config_t){
      .value_size = (SIZE_MAX / 3U) + 1U,
      .value_alignment = 1U,
      .capacity = 3U,
  };
  zassert_equal(wl_fifo_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);

  config = (wl_fifo_config_t){
      .value_size = 3U,
      .value_alignment = 4U,
      .capacity = 3U,
  };
  zassert_ok(wl_fifo_requirements(&config, &requirements));
  zassert_equal(requirements.slot_stride, 4U);
  zassert_equal(requirements.storage_size, 12U);
  zassert_equal(requirements.slot_count, 3U);

  storage = (wl_fifo_storage_t){bytes, requirements.storage_size - 1U};
  zassert_equal(wl_fifo_init(&fifo, &config, &storage), WL_ERR_BUF_TOO_SMALL);
  storage = (wl_fifo_storage_t){bytes + 1U, sizeof(bytes) - 1U};
  zassert_equal(wl_fifo_init(&fifo, &config, &storage), WL_ERR_INVALID_ARG);
  storage = (wl_fifo_storage_t){bytes, sizeof(bytes)};
  zassert_ok(wl_fifo_init(&fifo, &config, &storage));

  config = (wl_fifo_config_t){
      .value_size = 17U,
      .value_alignment = 32U,
      .capacity = 4U,
  };
  zassert_ok(wl_fifo_requirements(&config, &requirements));
  zassert_equal(requirements.slot_stride, 32U);
  zassert_equal(requirements.storage_size, sizeof(over_aligned));
  storage = (wl_fifo_storage_t){over_aligned, sizeof(over_aligned)};
  zassert_ok(wl_fifo_init(&fifo, &config, &storage));

  config = (wl_fifo_config_t){
      .value_size = 4U,
      .value_alignment = 4U,
      .capacity = 1U,
  };
  storage = (wl_fifo_storage_t){&fifo, sizeof(fifo)};
  zassert_equal(wl_fifo_init(&fifo, &config, &storage), WL_ERR_INVALID_ARG);

  zassert_equal(wl_fifo_init(NULL, &config, &storage), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fifo_init(&fifo, &config, NULL), WL_ERR_INVALID_ARG);
}

ZTEST(wirelink_fifo_unit, test_uninitialized_and_null_arguments) {
  wl_fifo_t fifo = {0};
  wl_fifo_write_claim_t claim = {0};
  wl_fifo_view_t view = {0};
  wl_fifo_stats_t stats = {0};

  zassert_equal(wl_fifo_write_claim(&fifo, &claim), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_fifo_write_publish(&fifo, &claim), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_fifo_write_abort(&fifo, &claim), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_fifo_read_acquire(&fifo, &view), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_fifo_read_release(&fifo, &view), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_fifo_reset(&fifo), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_fifo_get_stats(&fifo, &stats), WL_ERR_NOT_INITIALIZED);

  zassert_equal(wl_fifo_write_claim(NULL, &claim), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fifo_write_claim(&fifo, NULL), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fifo_read_acquire(NULL, &view), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fifo_read_acquire(&fifo, NULL), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fifo_get_stats(NULL, &stats), WL_ERR_INVALID_ARG);
  zassert_equal(wl_fifo_get_stats(&fifo, NULL), WL_ERR_INVALID_ARG);
}

ZTEST(wirelink_fifo_unit, test_order_full_and_borrow_lifetime) {
  struct fixture *test = fixture_init();
  wl_fifo_write_claim_t rejected;
  wl_fifo_view_t borrowed;
  wl_fifo_view_t view;
  wl_fifo_stats_t stats;

  memset(&view, 0xa5, sizeof(view));
  zassert_equal(wl_fifo_read_acquire(&test->fifo, &view), WL_ERR_NO_DATA);
  zassert_is_null(view.value);
  zassert_equal(view.value_size, 0U);

  publish_value(test, 1U);
  zassert_ok(wl_fifo_read_acquire(&test->fifo, &borrowed));
  expect_value(borrowed.value, 1U);
  publish_value(test, 2U);
  publish_value(test, 3U);
  expect_value(borrowed.value, 1U);

  memset(&rejected, 0xa5, sizeof(rejected));
  zassert_equal(wl_fifo_write_claim(&test->fifo, &rejected), WL_ERR_QUEUE_FULL);
  zassert_is_null(rejected.value);
  zassert_equal(rejected.value_size, 0U);
  expect_value(borrowed.value, 1U);

  zassert_ok(wl_fifo_get_stats(&test->fifo, &stats));
  zassert_equal(stats.depth, 3U);
  zassert_equal(stats.high_watermark, 3U);
  zassert_equal(stats.full_rejections, 1U);

  zassert_ok(wl_fifo_read_release(&test->fifo, &borrowed));
  publish_value(test, 4U);
  consume_value(test, 2U);
  consume_value(test, 3U);
  consume_value(test, 4U);
  zassert_equal(wl_fifo_read_acquire(&test->fifo, &view), WL_ERR_NO_DATA);

  zassert_ok(wl_fifo_get_stats(&test->fifo, &stats));
  zassert_equal(stats.depth, 0U);
  zassert_equal(stats.high_watermark, 3U);
  zassert_equal(stats.publishes, 4U);
  zassert_equal(stats.consumes, 4U);
  zassert_equal(stats.full_rejections, 1U);
  zassert_equal(stats.empty_reads, 2U);
  zassert_equal(stats.errors, 0U);
}

ZTEST(wirelink_fifo_unit, test_token_abort_and_reset_semantics) {
  struct fixture *test = fixture_init();
  wl_fifo_write_claim_t active_claim;
  wl_fifo_write_claim_t claim;
  wl_fifo_write_claim_t bad_claim;
  wl_fifo_view_t active_view;
  wl_fifo_view_t view;
  wl_fifo_view_t bad_view;
  wl_fifo_stats_t stats;

  zassert_ok(wl_fifo_write_claim(&test->fifo, &active_claim));
  zassert_equal(wl_fifo_write_claim(&test->fifo, &claim), WL_ERR_BUSY);
  zassert_is_null(claim.value);
  zassert_equal(wl_fifo_reset(&test->fifo), WL_ERR_BUSY);
  bad_claim = active_claim;
  bad_claim.token++;
  zassert_equal(wl_fifo_write_publish(&test->fifo, &bad_claim),
                WL_ERR_INVALID_STATE);
  zassert_ok(wl_fifo_write_abort(&test->fifo, &active_claim));
  zassert_equal(wl_fifo_write_abort(&test->fifo, &active_claim),
                WL_ERR_INVALID_STATE);

  publish_value(test, 1U);
  zassert_ok(wl_fifo_read_acquire(&test->fifo, &active_view));
  zassert_equal(wl_fifo_read_acquire(&test->fifo, &view), WL_ERR_BUSY);
  zassert_is_null(view.value);
  zassert_equal(wl_fifo_reset(&test->fifo), WL_ERR_BUSY);
  bad_view = active_view;
  bad_view.private_slot = (bad_view.private_slot + 1U) % TEST_CAPACITY;
  zassert_equal(wl_fifo_read_release(&test->fifo, &bad_view),
                WL_ERR_INVALID_STATE);
  zassert_ok(wl_fifo_read_release(&test->fifo, &active_view));
  zassert_equal(wl_fifo_read_release(&test->fifo, &active_view),
                WL_ERR_INVALID_STATE);

  publish_value(test, 2U);
  publish_value(test, 3U);
  zassert_ok(wl_fifo_reset(&test->fifo));
  zassert_equal(wl_fifo_read_acquire(&test->fifo, &view), WL_ERR_NO_DATA);

  zassert_ok(wl_fifo_get_stats(&test->fifo, &stats));
  zassert_equal(stats.depth, 0U);
  zassert_equal(stats.high_watermark, 2U);
  zassert_equal(stats.publishes, 3U);
  zassert_equal(stats.consumes, 1U);
  zassert_equal(stats.empty_reads, 1U);
  zassert_equal(stats.aborts, 1U);
  zassert_equal(stats.resets, 1U);
  zassert_equal(stats.errors, 8U);
}

ZTEST(wirelink_fifo_unit, test_cursor_wrap_preserves_order) {
  struct fixture *test = fixture_init();
  wl_fifo_stats_t stats;

  wl_fifo_test_seed_cursors(&test->fifo, UINT32_MAX - 1U);
  publish_value(test, 1U);
  publish_value(test, 2U);
  consume_value(test, 1U);
  publish_value(test, 3U);
  consume_value(test, 2U);
  consume_value(test, 3U);

  zassert_ok(wl_fifo_get_stats(&test->fifo, &stats));
  zassert_equal(stats.depth, 0U);
  zassert_equal(stats.high_watermark, 2U);
  zassert_equal(stats.publishes, 3U);
  zassert_equal(stats.consumes, 3U);
  zassert_equal(stats.errors, 0U);
}

ZTEST(wirelink_fifo_unit, test_observability_counters_saturate) {
  struct fixture *test = fixture_init();
  const wl_fifo_stats_t seed = {
      .publishes = UINT32_MAX,
      .consumes = UINT32_MAX,
      .full_rejections = UINT32_MAX,
      .empty_reads = UINT32_MAX,
      .aborts = UINT32_MAX,
      .resets = UINT32_MAX,
      .errors = UINT32_MAX,
  };
  wl_fifo_write_claim_t active_claim;
  wl_fifo_write_claim_t claim;
  wl_fifo_view_t view;
  wl_fifo_stats_t stats;

  wl_fifo_test_seed_saturating_counters(&test->fifo, &seed);
  zassert_equal(wl_fifo_read_acquire(&test->fifo, &view), WL_ERR_NO_DATA);
  zassert_ok(wl_fifo_write_claim(&test->fifo, &active_claim));
  zassert_equal(wl_fifo_write_claim(&test->fifo, &claim), WL_ERR_BUSY);
  zassert_ok(wl_fifo_write_abort(&test->fifo, &active_claim));
  publish_value(test, 1U);
  publish_value(test, 2U);
  publish_value(test, 3U);
  zassert_equal(wl_fifo_write_claim(&test->fifo, &claim), WL_ERR_QUEUE_FULL);
  consume_value(test, 1U);
  zassert_ok(wl_fifo_reset(&test->fifo));

  zassert_ok(wl_fifo_get_stats(&test->fifo, &stats));
  zassert_equal(stats.depth, 0U);
  zassert_equal(stats.high_watermark, 3U);
  zassert_equal(stats.publishes, UINT32_MAX);
  zassert_equal(stats.consumes, UINT32_MAX);
  zassert_equal(stats.full_rejections, UINT32_MAX);
  zassert_equal(stats.empty_reads, UINT32_MAX);
  zassert_equal(stats.aborts, UINT32_MAX);
  zassert_equal(stats.resets, UINT32_MAX);
  zassert_equal(stats.errors, UINT32_MAX);
}

ZTEST_SUITE(wirelink_fifo_unit, NULL, NULL, NULL, NULL, NULL);
