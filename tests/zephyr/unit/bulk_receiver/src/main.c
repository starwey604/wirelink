/* SPDX-License-Identifier: Apache-2.0 */

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/bulk.h"

#define TEST_STORAGE_SIZE 128U

void wl_bulk_receiver_test_seed_stats(wl_bulk_receiver_t *receiver,
                                      const wl_bulk_receiver_stats_t *stats);

struct sink_fixture {
  wl_bulk_sink_result_t begin_result;
  wl_bulk_sink_result_t write_result;
  wl_bulk_sink_result_t finish_result;
  uint64_t resume_offset;
  wl_bulk_descriptor_t last_descriptor;
  const uint8_t *last_write_data;
  uint64_t last_write_offset;
  size_t last_write_length;
  int32_t last_abort_reason;
  uint32_t last_abort_transfer_id;
  uint32_t begin_calls;
  uint32_t write_calls;
  uint32_t finish_calls;
  uint32_t finish_successes;
  uint32_t abort_calls;
  bool invalid_callback_input;
  uint8_t storage[TEST_STORAGE_SIZE];
};

struct receiver_fixture {
  wl_bulk_receiver_t receiver;
  struct sink_fixture sink;
};

static struct receiver_fixture fixture;

static wl_bulk_sink_result_t sink_begin(void *user_data,
                                        const wl_bulk_descriptor_t *descriptor,
                                        uint64_t *out_resume_offset) {
  struct sink_fixture *sink = user_data;

  ++sink->begin_calls;
  sink->last_descriptor = *descriptor;
  *out_resume_offset = sink->resume_offset;
  return sink->begin_result;
}

static wl_bulk_sink_result_t sink_write(void *user_data, uint32_t transfer_id,
                                        uint64_t offset, const uint8_t *data,
                                        size_t length) {
  struct sink_fixture *sink = user_data;

  ++sink->write_calls;
  sink->last_abort_transfer_id = transfer_id;
  sink->last_write_data = data;
  sink->last_write_offset = offset;
  sink->last_write_length = length;
  if (sink->write_result == WL_BULK_SINK_OK) {
    if (offset > sizeof(sink->storage) ||
        length > sizeof(sink->storage) - (size_t)offset) {
      sink->invalid_callback_input = true;
      return WL_BULK_SINK_INVALID;
    }
    memcpy(&sink->storage[(size_t)offset], data, length);
  }
  return sink->write_result;
}

static wl_bulk_sink_result_t
sink_finish(void *user_data, const wl_bulk_descriptor_t *descriptor) {
  struct sink_fixture *sink = user_data;

  ++sink->finish_calls;
  sink->last_descriptor = *descriptor;
  if (sink->finish_result == WL_BULK_SINK_OK) {
    ++sink->finish_successes;
  }
  return sink->finish_result;
}

static void sink_abort(void *user_data, uint32_t transfer_id, int32_t reason) {
  struct sink_fixture *sink = user_data;

  ++sink->abort_calls;
  sink->last_abort_transfer_id = transfer_id;
  sink->last_abort_reason = reason;
}

static wl_bulk_receiver_config_t receiver_config(uint32_t timeout_ms) {
  return (wl_bulk_receiver_config_t){
      .max_object_length = TEST_STORAGE_SIZE,
      .max_chunk_size = 16U,
      .write_alignment = 4U,
      .idle_timeout_ms = timeout_ms,
      .sink =
          {
              .user_data = &fixture.sink,
              .begin = sink_begin,
              .write = sink_write,
              .finish = sink_finish,
              .abort = sink_abort,
          },
  };
}

static void receiver_initialize(uint32_t timeout_ms) {
  wl_bulk_receiver_config_t config;

  memset(&fixture, 0, sizeof(fixture));
  fixture.sink.begin_result = WL_BULK_SINK_OK;
  fixture.sink.write_result = WL_BULK_SINK_OK;
  fixture.sink.finish_result = WL_BULK_SINK_OK;
  config = receiver_config(timeout_ms);
  zassert_equal(wl_bulk_receiver_init(&fixture.receiver, &config), WL_BULK_OK);
}

static wl_bulk_descriptor_t descriptor(uint32_t transfer_id,
                                       uint64_t total_length,
                                       uint32_t requested_chunk_size,
                                       uint32_t crc32c) {
  return (wl_bulk_descriptor_t){
      .transfer_id = transfer_id,
      .total_length = total_length,
      .requested_chunk_size = requested_chunk_size,
      .object_crc32c = crc32c,
  };
}

static wl_bulk_chunk_t chunk(uint32_t transfer_id, uint64_t offset,
                             const uint8_t *data, size_t length) {
  return (wl_bulk_chunk_t){
      .transfer_id = transfer_id,
      .offset = offset,
      .data = data,
      .length = length,
  };
}

static wl_bulk_receiver_status_view_t
acquire_status(uint32_t transfer_id, wl_bulk_phase_t phase,
               wl_bulk_status_code_t code, uint64_t next_offset,
               uint32_t accepted_chunk_size) {
  wl_bulk_receiver_status_view_t view;

  zassert_equal(wl_bulk_receiver_status_acquire(&fixture.receiver, &view),
                WL_BULK_OK);
  zassert_equal(view.status.transfer_id, transfer_id);
  zassert_equal(view.status.phase, phase);
  zassert_equal(view.status.code, code);
  zassert_equal(view.status.next_offset, next_offset);
  zassert_equal(view.status.accepted_chunk_size, accepted_chunk_size);
  zassert_not_equal(view.token, 0U);
  return view;
}

static void release_status(uint32_t transfer_id, wl_bulk_phase_t phase,
                           wl_bulk_status_code_t code, uint64_t next_offset,
                           uint32_t accepted_chunk_size) {
  const wl_bulk_receiver_status_view_t view = acquire_status(
      transfer_id, phase, code, next_offset, accepted_chunk_size);

  zassert_equal(wl_bulk_receiver_status_release(&fixture.receiver, &view),
                WL_BULK_OK);
}

static void expect_state(wl_bulk_receiver_state_t expected_state,
                         uint64_t expected_offset) {
  wl_bulk_receiver_state_t state;
  uint64_t offset;

  zassert_equal(wl_bulk_receiver_get_state(&fixture.receiver, &state, &offset),
                WL_BULK_OK);
  zassert_equal(state, expected_state);
  zassert_equal(offset, expected_offset);
}

ZTEST(wirelink_bulk_receiver, test_init_validation_and_reset_lifecycle) {
  wl_bulk_receiver_t uninitialized = {0};
  wl_bulk_receiver_config_t config = receiver_config(10U);
  wl_bulk_receiver_status_view_t view = {.token = 123U};
  wl_bulk_receiver_state_t state = WL_BULK_RECEIVER_FAILED;
  wl_bulk_receiver_stats_t stats = {.begins = 1U};
  wl_bulk_deadline_hint_t hint = {.next_deadline_ms = 1U};
  uint64_t offset = 123U;
  wl_bulk_descriptor_t begin = descriptor(1U, 8U, 8U, 0x1234U);

  zassert_true(sizeof(wl_bulk_receiver_t) <= 256U);
  zassert_equal(wl_bulk_receiver_reset(NULL), WL_BULK_ERR_INVALID_ARG);
  zassert_equal(wl_bulk_receiver_reset(&uninitialized),
                WL_BULK_ERR_NOT_INITIALIZED);
  zassert_equal(wl_bulk_receiver_status_acquire(&uninitialized, &view),
                WL_BULK_ERR_NOT_INITIALIZED);
  zassert_equal(view.token, 0U);
  zassert_equal(wl_bulk_receiver_get_state(&uninitialized, &state, &offset),
                WL_BULK_ERR_NOT_INITIALIZED);
  zassert_equal(state, WL_BULK_RECEIVER_IDLE);
  zassert_equal(offset, 0U);
  zassert_equal(wl_bulk_receiver_get_stats(&uninitialized, &stats),
                WL_BULK_ERR_NOT_INITIALIZED);
  zassert_equal(stats.begins, 0U);
  zassert_equal(wl_bulk_receiver_get_deadline_hint(&uninitialized, 0U, &hint),
                WL_BULK_ERR_NOT_INITIALIZED);
  zassert_equal(hint.next_deadline_ms, WL_BULK_NO_DEADLINE_MS);

  zassert_equal(wl_bulk_receiver_init(NULL, &config), WL_BULK_ERR_INVALID_ARG);
  zassert_equal(wl_bulk_receiver_init(&uninitialized, NULL),
                WL_BULK_ERR_INVALID_ARG);
  config.max_object_length = 0U;
  zassert_equal(wl_bulk_receiver_init(&uninitialized, &config),
                WL_BULK_ERR_INVALID_ARG);
  config = receiver_config(10U);
  config.max_chunk_size = 2U;
  zassert_equal(wl_bulk_receiver_init(&uninitialized, &config),
                WL_BULK_ERR_INVALID_ARG);
  config = receiver_config(10U);
  config.write_alignment = 3U;
  zassert_equal(wl_bulk_receiver_init(&uninitialized, &config),
                WL_BULK_ERR_INVALID_ARG);
  config = receiver_config(UINT32_C(0x80000000));
  zassert_equal(wl_bulk_receiver_init(&uninitialized, &config),
                WL_BULK_ERR_INVALID_ARG);
  config = receiver_config(10U);
  config.sink.write = NULL;
  zassert_equal(wl_bulk_receiver_init(&uninitialized, &config),
                WL_BULK_ERR_INVALID_ARG);

  zassert_equal(strcmp(wl_bulk_err_str(WL_BULK_OK), "ok"), 0);
  zassert_equal(strcmp(wl_bulk_err_str(WL_BULK_ERR_TIMEOUT), "timed out"), 0);
  zassert_equal(strcmp(wl_bulk_err_str(42), "unknown bulk error"), 0);

  receiver_initialize(10U);
  expect_state(WL_BULK_RECEIVER_IDLE, 0U);
  zassert_equal(wl_bulk_receiver_status_acquire(&fixture.receiver, &view),
                WL_BULK_ERR_NOT_FOUND);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 1U),
                WL_BULK_OK);
  view = acquire_status(1U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 8U);
  zassert_equal(wl_bulk_receiver_reset(&fixture.receiver), WL_BULK_ERR_BUSY);
  zassert_equal(fixture.sink.abort_calls, 0U);
  zassert_equal(wl_bulk_receiver_status_release(&fixture.receiver, &view),
                WL_BULK_OK);
  zassert_equal(wl_bulk_receiver_reset(&fixture.receiver), WL_BULK_OK);
  zassert_equal(fixture.sink.abort_calls, 1U);
  zassert_equal(fixture.sink.last_abort_transfer_id, 1U);
  zassert_equal(fixture.sink.last_abort_reason, WL_BULK_ERR_INVALID_STATE);
  expect_state(WL_BULK_RECEIVER_IDLE, 0U);
}

ZTEST(wirelink_bulk_receiver,
      test_begin_resume_duplicate_and_status_token_ownership) {
  const uint8_t bytes[4] = {1U, 2U, 3U, 4U};
  wl_bulk_descriptor_t begin = descriptor(7U, 20U, 14U, 0xaabbccddU);
  wl_bulk_descriptor_t conflict;
  wl_bulk_chunk_t blocked = chunk(7U, 8U, bytes, sizeof(bytes));
  wl_bulk_receiver_status_view_t active;
  wl_bulk_receiver_status_view_t reacquired;
  wl_bulk_receiver_status_view_t empty = {.token = 99U};
  wl_bulk_receiver_status_view_t stale;
  wl_bulk_receiver_stats_t stats;

  receiver_initialize(25U);
  fixture.sink.resume_offset = 8U;
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 10U),
                WL_BULK_OK);
  zassert_equal(fixture.sink.begin_calls, 1U);
  expect_state(WL_BULK_RECEIVER_RECEIVING, 8U);
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &blocked, 11U),
                WL_BULK_ERR_BUSY);
  zassert_equal(fixture.sink.write_calls, 0U);

  active = acquire_status(7U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 8U, 12U);
  zassert_equal(wl_bulk_receiver_status_acquire(&fixture.receiver, &empty),
                WL_BULK_ERR_BUSY);
  zassert_equal(empty.token, 0U);
  stale = active;
  ++stale.token;
  zassert_equal(wl_bulk_receiver_status_defer(&fixture.receiver, &stale),
                WL_BULK_ERR_INVALID_STATE);
  zassert_equal(wl_bulk_receiver_status_defer(&fixture.receiver, &active),
                WL_BULK_OK);
  reacquired =
      acquire_status(7U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 8U, 12U);
  zassert_equal(reacquired.token, active.token);
  zassert_equal(wl_bulk_receiver_status_release(&fixture.receiver, &reacquired),
                WL_BULK_OK);
  zassert_equal(wl_bulk_receiver_status_release(&fixture.receiver, &active),
                WL_BULK_ERR_INVALID_STATE);

  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 12U),
                WL_BULK_OK);
  zassert_equal(fixture.sink.begin_calls, 1U);
  release_status(7U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 8U, 12U);

  conflict = begin;
  ++conflict.object_crc32c;
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &conflict, 13U),
                WL_BULK_OK);
  zassert_equal(fixture.sink.begin_calls, 1U);
  release_status(7U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_CONFLICT, 8U, 12U);

  zassert_equal(wl_bulk_receiver_get_stats(&fixture.receiver, &stats),
                WL_BULK_OK);
  zassert_equal(stats.begins, 1U);
  zassert_equal(stats.duplicate_messages, 1U);
  zassert_equal(stats.protocol_errors, 1U);
}

ZTEST(wirelink_bulk_receiver,
      test_full_validation_precedes_callbacks_and_small_final_chunk) {
  const uint8_t bytes[8] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
  wl_bulk_descriptor_t invalid = descriptor(0U, 8U, 8U, 1U);
  wl_bulk_descriptor_t valid = descriptor(2U, 2U, 2U, 2U);
  wl_bulk_chunk_t final_chunk = chunk(2U, 0U, bytes, 2U);

  receiver_initialize(0U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &invalid, 0U),
                WL_BULK_OK);
  release_status(0U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_INVALID, 0U, 0U);
  invalid = descriptor(1U, TEST_STORAGE_SIZE + 1U, 8U, 1U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &invalid, 0U),
                WL_BULK_OK);
  release_status(1U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_INVALID, 0U, 0U);
  invalid = descriptor(1U, 8U, 0U, 1U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &invalid, 0U),
                WL_BULK_OK);
  release_status(1U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_INVALID, 0U, 0U);
  invalid = descriptor(1U, 8U, 2U, 1U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &invalid, 0U),
                WL_BULK_OK);
  release_status(1U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_INVALID, 0U, 0U);
  zassert_equal(fixture.sink.begin_calls, 0U);

  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &valid, 0U),
                WL_BULK_OK);
  release_status(2U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 2U);
  zassert_equal(wl_bulk_receiver_on_end(&fixture.receiver, 2U, 2U, 2U, 0U),
                WL_BULK_OK);
  release_status(2U, WL_BULK_PHASE_END, WL_BULK_STATUS_OUT_OF_ORDER, 0U, 2U);
  zassert_equal(fixture.sink.finish_calls, 0U);
  zassert_equal(wl_bulk_receiver_on_abort(&fixture.receiver, 3U, 9, 0U),
                WL_BULK_OK);
  release_status(3U, WL_BULK_PHASE_ABORT, WL_BULK_STATUS_CONFLICT, 0U, 2U);
  zassert_equal(fixture.sink.abort_calls, 0U);
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &final_chunk, 1U),
                WL_BULK_OK);
  zassert_equal(fixture.sink.write_calls, 1U);
  zassert_equal(fixture.sink.last_write_data, bytes);
  release_status(2U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 2U, 2U);
  zassert_equal(wl_bulk_receiver_on_end(&fixture.receiver, 2U, 2U, 3U, 2U),
                WL_BULK_OK);
  release_status(2U, WL_BULK_PHASE_END, WL_BULK_STATUS_CONFLICT, 2U, 2U);
  zassert_equal(fixture.sink.finish_calls, 0U);
  zassert_equal(wl_bulk_receiver_on_end(&fixture.receiver, 2U, 2U, 2U, 2U),
                WL_BULK_OK);
  release_status(2U, WL_BULK_PHASE_END, WL_BULK_STATUS_OK, 2U, 2U);
  zassert_equal(fixture.sink.finish_calls, 1U);
}

ZTEST(wirelink_bulk_receiver,
      test_sequential_bounds_duplicates_and_terminal_idempotence) {
  const uint8_t first[8] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
  const uint8_t second[8] = {8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U};
  const uint8_t final[4] = {16U, 17U, 18U, 19U};
  uint8_t expected[20];
  wl_bulk_descriptor_t begin = descriptor(11U, 20U, 8U, 0x98765432U);
  wl_bulk_chunk_t input;
  wl_bulk_receiver_stats_t stats;

  memcpy(expected, first, sizeof(first));
  memcpy(&expected[8], second, sizeof(second));
  memcpy(&expected[16], final, sizeof(final));
  receiver_initialize(100U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 0U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 8U);

  input = chunk(11U, 8U, second, sizeof(second));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 1U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OUT_OF_ORDER, 0U, 8U);
  input = chunk(11U, 0U, first, sizeof(first));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 2U),
                WL_BULK_OK);
  zassert_equal(fixture.sink.last_write_data, first);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 8U, 8U);
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 3U),
                WL_BULK_OK);
  zassert_equal(fixture.sink.write_calls, 1U);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 8U, 8U);

  input = chunk(11U, 4U, second, sizeof(second));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 4U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OUT_OF_ORDER, 8U, 8U);
  input = chunk(11U, 10U, second, 4U);
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 5U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_INVALID, 8U, 8U);
  input = chunk(11U, 8U, second, 6U);
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 6U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_INVALID, 8U, 8U);
  input = chunk(11U, 16U, second, sizeof(second));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 7U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_INVALID, 8U, 8U);
  input = chunk(11U, UINT64_MAX - 3U, second, sizeof(second));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 8U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_INVALID, 8U, 8U);
  input = chunk(11U, 8U, second, sizeof(second) + 1U);
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 9U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_INVALID, 8U, 8U);
  input = chunk(12U, 8U, second, sizeof(second));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 10U),
                WL_BULK_OK);
  release_status(12U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_CONFLICT, 8U, 8U);
  zassert_equal(fixture.sink.write_calls, 1U);

  input = chunk(11U, 8U, second, sizeof(second));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 11U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 16U, 8U);
  input = chunk(11U, 16U, final, sizeof(final));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 12U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 20U, 8U);
  zassert_mem_equal(fixture.sink.storage, expected, sizeof(expected));
  zassert_false(fixture.sink.invalid_callback_input);

  zassert_equal(
      wl_bulk_receiver_on_end(&fixture.receiver, 11U, 20U, 0x98765432U, 13U),
      WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_END, WL_BULK_STATUS_OK, 20U, 8U);
  expect_state(WL_BULK_RECEIVER_COMPLETED, 20U);
  zassert_equal(
      wl_bulk_receiver_on_end(&fixture.receiver, 11U, 20U, 0x98765432U, 14U),
      WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_END, WL_BULK_STATUS_OK, 20U, 8U);
  zassert_equal(fixture.sink.finish_calls, 1U);
  zassert_equal(fixture.sink.finish_successes, 1U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 15U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 20U, 8U);
  input = chunk(11U, 8U, second, sizeof(second));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 16U),
                WL_BULK_OK);
  release_status(11U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 20U, 8U);
  zassert_equal(fixture.sink.begin_calls, 1U);
  zassert_equal(fixture.sink.write_calls, 3U);

  zassert_equal(wl_bulk_receiver_get_stats(&fixture.receiver, &stats),
                WL_BULK_OK);
  zassert_equal(stats.begins, 1U);
  zassert_equal(stats.chunks, 3U);
  zassert_equal(stats.bytes_written, 20U);
  zassert_equal(stats.duplicate_messages, 4U);
  zassert_equal(stats.protocol_errors, 8U);
}

ZTEST(wirelink_bulk_receiver, test_busy_consumes_nothing_and_finish_once) {
  const uint8_t bytes[8] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
  wl_bulk_descriptor_t begin = descriptor(21U, 8U, 8U, 21U);
  wl_bulk_chunk_t input = chunk(21U, 0U, bytes, sizeof(bytes));
  wl_bulk_receiver_stats_t stats;

  receiver_initialize(50U);
  fixture.sink.begin_result = WL_BULK_SINK_BUSY;
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 0U),
                WL_BULK_OK);
  release_status(21U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_BUSY, 0U, 8U);
  expect_state(WL_BULK_RECEIVER_IDLE, 0U);
  fixture.sink.begin_result = WL_BULK_SINK_OK;
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 1U),
                WL_BULK_OK);
  release_status(21U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 8U);

  fixture.sink.write_result = WL_BULK_SINK_BUSY;
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 2U),
                WL_BULK_OK);
  release_status(21U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_BUSY, 0U, 8U);
  expect_state(WL_BULK_RECEIVER_RECEIVING, 0U);
  zassert_mem_equal(fixture.sink.storage, (uint8_t[8]){0}, 8U);
  fixture.sink.write_result = WL_BULK_SINK_OK;
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 3U),
                WL_BULK_OK);
  release_status(21U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 8U, 8U);

  fixture.sink.finish_result = WL_BULK_SINK_BUSY;
  zassert_equal(wl_bulk_receiver_on_end(&fixture.receiver, 21U, 8U, 21U, 4U),
                WL_BULK_OK);
  release_status(21U, WL_BULK_PHASE_END, WL_BULK_STATUS_BUSY, 8U, 8U);
  expect_state(WL_BULK_RECEIVER_RECEIVING, 8U);
  fixture.sink.finish_result = WL_BULK_SINK_OK;
  zassert_equal(wl_bulk_receiver_on_end(&fixture.receiver, 21U, 8U, 21U, 5U),
                WL_BULK_OK);
  release_status(21U, WL_BULK_PHASE_END, WL_BULK_STATUS_OK, 8U, 8U);
  zassert_equal(wl_bulk_receiver_on_end(&fixture.receiver, 21U, 8U, 21U, 6U),
                WL_BULK_OK);
  release_status(21U, WL_BULK_PHASE_END, WL_BULK_STATUS_OK, 8U, 8U);

  zassert_equal(fixture.sink.begin_calls, 2U);
  zassert_equal(fixture.sink.write_calls, 2U);
  zassert_equal(fixture.sink.finish_calls, 2U);
  zassert_equal(fixture.sink.finish_successes, 1U);
  zassert_equal(wl_bulk_receiver_get_stats(&fixture.receiver, &stats),
                WL_BULK_OK);
  zassert_equal(stats.begins, 1U);
  zassert_equal(stats.chunks, 1U);
  zassert_equal(stats.bytes_written, 8U);
  zassert_equal(stats.busy_responses, 3U);
  zassert_equal(stats.duplicate_messages, 1U);
}

ZTEST(wirelink_bulk_receiver, test_failures_abort_and_invalid_resume) {
  const uint8_t bytes[8] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
  wl_bulk_descriptor_t begin = descriptor(31U, 16U, 8U, 31U);
  wl_bulk_chunk_t input = chunk(31U, 0U, bytes, sizeof(bytes));
  wl_bulk_receiver_stats_t stats;

  receiver_initialize(20U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 0U),
                WL_BULK_OK);
  release_status(31U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 8U);
  fixture.sink.write_result = WL_BULK_SINK_WRITE_FAILED;
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 1U),
                WL_BULK_OK);
  release_status(31U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_WRITE_FAILED, 0U, 8U);
  expect_state(WL_BULK_RECEIVER_FAILED, 0U);
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 2U),
                WL_BULK_OK);
  release_status(31U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_WRITE_FAILED, 0U, 8U);
  zassert_equal(fixture.sink.write_calls, 1U);
  zassert_equal(wl_bulk_receiver_on_abort(&fixture.receiver, 31U, 77, 3U),
                WL_BULK_OK);
  release_status(31U, WL_BULK_PHASE_ABORT, WL_BULK_STATUS_ABORTED, 0U, 8U);
  zassert_equal(wl_bulk_receiver_on_abort(&fixture.receiver, 31U, 77, 4U),
                WL_BULK_OK);
  release_status(31U, WL_BULK_PHASE_ABORT, WL_BULK_STATUS_ABORTED, 0U, 8U);
  zassert_equal(fixture.sink.abort_calls, 1U);
  zassert_equal(fixture.sink.last_abort_reason, 77);
  expect_state(WL_BULK_RECEIVER_ABORTED, 0U);
  zassert_equal(wl_bulk_receiver_get_stats(&fixture.receiver, &stats),
                WL_BULK_OK);
  zassert_equal(stats.write_failures, 1U);
  zassert_equal(stats.aborts, 1U);
  zassert_equal(stats.duplicate_messages, 1U);

  zassert_equal(wl_bulk_receiver_reset(&fixture.receiver), WL_BULK_OK);
  fixture.sink.begin_result = WL_BULK_SINK_OK;
  fixture.sink.resume_offset = 6U;
  begin = descriptor(32U, 16U, 8U, 32U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 5U),
                WL_BULK_OK);
  release_status(32U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_INVALID, 0U, 8U);
  expect_state(WL_BULK_RECEIVER_FAILED, 0U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 6U),
                WL_BULK_OK);
  release_status(32U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_INVALID, 0U, 8U);
  zassert_equal(fixture.sink.begin_calls, 2U);
  zassert_equal(wl_bulk_receiver_reset(&fixture.receiver), WL_BULK_OK);
  zassert_equal(fixture.sink.abort_calls, 2U);
}

ZTEST(wirelink_bulk_receiver, test_end_integrity_failure_is_idempotent) {
  wl_bulk_descriptor_t begin = descriptor(41U, 0U, 8U, 41U);
  wl_bulk_receiver_stats_t stats;

  receiver_initialize(20U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 0U),
                WL_BULK_OK);
  release_status(41U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 8U);
  fixture.sink.finish_result = WL_BULK_SINK_INTEGRITY_FAILED;
  zassert_equal(wl_bulk_receiver_on_end(&fixture.receiver, 41U, 0U, 41U, 1U),
                WL_BULK_OK);
  release_status(41U, WL_BULK_PHASE_END, WL_BULK_STATUS_INTEGRITY_FAILED, 0U,
                 8U);
  expect_state(WL_BULK_RECEIVER_FAILED, 0U);
  zassert_equal(wl_bulk_receiver_on_end(&fixture.receiver, 41U, 0U, 41U, 2U),
                WL_BULK_OK);
  release_status(41U, WL_BULK_PHASE_END, WL_BULK_STATUS_INTEGRITY_FAILED, 0U,
                 8U);
  zassert_equal(fixture.sink.finish_calls, 1U);
  zassert_equal(wl_bulk_receiver_get_stats(&fixture.receiver, &stats),
                WL_BULK_OK);
  zassert_equal(stats.integrity_failures, 1U);
  zassert_equal(stats.duplicate_messages, 1U);
}

ZTEST(wirelink_bulk_receiver, test_timeout_deadline_is_wrap_safe) {
  wl_bulk_descriptor_t begin = descriptor(51U, 8U, 8U, 51U);
  wl_bulk_deadline_hint_t hint;
  wl_bulk_receiver_stats_t stats;
  const wl_time_ms_t started_at = UINT32_MAX - 4U;

  receiver_initialize(10U);
  zassert_equal(
      wl_bulk_receiver_on_begin(&fixture.receiver, &begin, started_at),
      WL_BULK_OK);
  zassert_equal(
      wl_bulk_receiver_get_deadline_hint(&fixture.receiver, 2U, &hint),
      WL_BULK_OK);
  zassert_equal(hint.next_deadline_ms, WL_BULK_NO_DEADLINE_MS);
  release_status(51U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 8U);
  zassert_equal(
      wl_bulk_receiver_get_deadline_hint(&fixture.receiver, 2U, &hint),
      WL_BULK_OK);
  zassert_equal(hint.next_deadline_ms, 3U);
  zassert_equal(wl_bulk_receiver_poll(&fixture.receiver, 4U), WL_BULK_OK);
  zassert_equal(fixture.sink.abort_calls, 0U);
  zassert_equal(
      wl_bulk_receiver_get_deadline_hint(&fixture.receiver, 4U, &hint),
      WL_BULK_OK);
  zassert_equal(hint.next_deadline_ms, 1U);
  zassert_equal(wl_bulk_receiver_poll(&fixture.receiver, 5U), WL_BULK_OK);
  zassert_equal(fixture.sink.abort_calls, 1U);
  zassert_equal(fixture.sink.last_abort_reason, WL_BULK_ERR_TIMEOUT);
  release_status(51U, WL_BULK_PHASE_ABORT, WL_BULK_STATUS_TIMED_OUT, 0U, 8U);
  expect_state(WL_BULK_RECEIVER_FAILED, 0U);
  zassert_equal(wl_bulk_receiver_poll(&fixture.receiver, 100U), WL_BULK_OK);
  zassert_equal(fixture.sink.abort_calls, 1U);
  zassert_equal(wl_bulk_receiver_get_stats(&fixture.receiver, &stats),
                WL_BULK_OK);
  zassert_equal(stats.timeouts, 1U);
}

ZTEST(wirelink_bulk_receiver,
      test_new_transfer_replaces_each_released_terminal_state) {
  const uint8_t bytes[4] = {0U, 1U, 2U, 3U};
  wl_bulk_descriptor_t begin = descriptor(71U, 0U, 4U, 71U);
  wl_bulk_chunk_t input;
  wl_bulk_receiver_stats_t stats;

  receiver_initialize(20U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 0U),
                WL_BULK_OK);
  release_status(71U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  zassert_equal(wl_bulk_receiver_on_end(&fixture.receiver, 71U, 0U, 71U, 1U),
                WL_BULK_OK);
  release_status(71U, WL_BULK_PHASE_END, WL_BULK_STATUS_OK, 0U, 4U);
  expect_state(WL_BULK_RECEIVER_COMPLETED, 0U);

  begin = descriptor(72U, 4U, 4U, 72U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 2U),
                WL_BULK_OK);
  release_status(72U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  expect_state(WL_BULK_RECEIVER_RECEIVING, 0U);
  zassert_equal(fixture.sink.finish_calls, 1U);
  zassert_equal(fixture.sink.abort_calls, 0U);
  fixture.sink.write_result = WL_BULK_SINK_WRITE_FAILED;
  input = chunk(72U, 0U, bytes, sizeof(bytes));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 3U),
                WL_BULK_OK);
  release_status(72U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_WRITE_FAILED, 0U, 4U);
  expect_state(WL_BULK_RECEIVER_FAILED, 0U);

  fixture.sink.write_result = WL_BULK_SINK_OK;
  begin = descriptor(73U, 4U, 4U, 73U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 4U),
                WL_BULK_OK);
  release_status(73U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  expect_state(WL_BULK_RECEIVER_RECEIVING, 0U);
  zassert_equal(fixture.sink.finish_calls, 1U);
  zassert_equal(fixture.sink.abort_calls, 0U);
  zassert_equal(wl_bulk_receiver_on_abort(&fixture.receiver, 73U, 73, 5U),
                WL_BULK_OK);
  release_status(73U, WL_BULK_PHASE_ABORT, WL_BULK_STATUS_ABORTED, 0U, 4U);
  expect_state(WL_BULK_RECEIVER_ABORTED, 0U);
  zassert_equal(fixture.sink.abort_calls, 1U);

  begin = descriptor(74U, 4U, 4U, 74U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 6U),
                WL_BULK_OK);
  release_status(74U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  expect_state(WL_BULK_RECEIVER_RECEIVING, 0U);
  zassert_equal(fixture.sink.finish_calls, 1U);
  zassert_equal(fixture.sink.abort_calls, 1U);
  zassert_equal(fixture.sink.begin_calls, 4U);
  zassert_equal(wl_bulk_receiver_get_stats(&fixture.receiver, &stats),
                WL_BULK_OK);
  zassert_equal(stats.begins, 4U);
  zassert_equal(stats.write_failures, 1U);
  zassert_equal(stats.aborts, 1U);
}

ZTEST(wirelink_bulk_receiver, test_all_observability_counters_saturate) {
  const uint8_t bytes[4] = {0U, 1U, 2U, 3U};
  const wl_bulk_receiver_stats_t saturated = {
      .begins = UINT32_MAX,
      .chunks = UINT32_MAX,
      .bytes_written = UINT32_MAX,
      .duplicate_messages = UINT32_MAX,
      .busy_responses = UINT32_MAX,
      .protocol_errors = UINT32_MAX,
      .write_failures = UINT32_MAX,
      .integrity_failures = UINT32_MAX,
      .aborts = UINT32_MAX,
      .timeouts = UINT32_MAX,
  };
  wl_bulk_receiver_stats_t stats;
  wl_bulk_descriptor_t begin = descriptor(61U, 8U, 4U, 61U);
  wl_bulk_chunk_t input;

  receiver_initialize(1U);
  wl_bulk_receiver_test_seed_stats(&fixture.receiver, &saturated);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 0U),
                WL_BULK_OK);
  release_status(61U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 0U),
                WL_BULK_OK);
  release_status(61U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  input = chunk(61U, 4U, bytes, sizeof(bytes));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 0U),
                WL_BULK_OK);
  release_status(61U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OUT_OF_ORDER, 0U, 4U);
  fixture.sink.write_result = WL_BULK_SINK_BUSY;
  input = chunk(61U, 0U, bytes, sizeof(bytes));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 0U),
                WL_BULK_OK);
  release_status(61U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_BUSY, 0U, 4U);
  fixture.sink.write_result = WL_BULK_SINK_OK;
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 0U),
                WL_BULK_OK);
  release_status(61U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 4U, 4U);
  fixture.sink.write_result = WL_BULK_SINK_WRITE_FAILED;
  input = chunk(61U, 4U, bytes, sizeof(bytes));
  zassert_equal(wl_bulk_receiver_on_chunk(&fixture.receiver, &input, 0U),
                WL_BULK_OK);
  release_status(61U, WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_WRITE_FAILED, 4U, 4U);

  zassert_equal(wl_bulk_receiver_reset(&fixture.receiver), WL_BULK_OK);
  fixture.sink.begin_result = WL_BULK_SINK_OK;
  fixture.sink.write_result = WL_BULK_SINK_OK;
  fixture.sink.finish_result = WL_BULK_SINK_INTEGRITY_FAILED;
  begin = descriptor(62U, 0U, 4U, 62U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 0U),
                WL_BULK_OK);
  release_status(62U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  zassert_equal(wl_bulk_receiver_on_end(&fixture.receiver, 62U, 0U, 62U, 0U),
                WL_BULK_OK);
  release_status(62U, WL_BULK_PHASE_END, WL_BULK_STATUS_INTEGRITY_FAILED, 0U,
                 4U);

  zassert_equal(wl_bulk_receiver_reset(&fixture.receiver), WL_BULK_OK);
  fixture.sink.finish_result = WL_BULK_SINK_OK;
  begin = descriptor(63U, 4U, 4U, 63U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 0U),
                WL_BULK_OK);
  release_status(63U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  zassert_equal(wl_bulk_receiver_on_abort(&fixture.receiver, 63U, 1, 0U),
                WL_BULK_OK);
  release_status(63U, WL_BULK_PHASE_ABORT, WL_BULK_STATUS_ABORTED, 0U, 4U);

  zassert_equal(wl_bulk_receiver_reset(&fixture.receiver), WL_BULK_OK);
  begin = descriptor(64U, 4U, 4U, 64U);
  zassert_equal(wl_bulk_receiver_on_begin(&fixture.receiver, &begin, 0U),
                WL_BULK_OK);
  release_status(64U, WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  zassert_equal(wl_bulk_receiver_poll(&fixture.receiver, 1U), WL_BULK_OK);
  release_status(64U, WL_BULK_PHASE_ABORT, WL_BULK_STATUS_TIMED_OUT, 0U, 4U);

  zassert_equal(wl_bulk_receiver_get_stats(&fixture.receiver, &stats),
                WL_BULK_OK);
  zassert_mem_equal(&stats, &saturated, sizeof(stats));
}

ZTEST_SUITE(wirelink_bulk_receiver, NULL, NULL, NULL, NULL, NULL);
