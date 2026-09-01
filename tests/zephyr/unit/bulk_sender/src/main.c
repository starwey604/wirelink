/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/bulk.h"

_Static_assert(sizeof(wl_bulk_sender_t) == WL_BULK_SENDER_STORAGE_SIZE,
               "sender ABI storage size changed");
_Static_assert(_Alignof(wl_bulk_sender_t) >= _Alignof(max_align_t),
               "sender ABI storage is under-aligned");
_Static_assert(sizeof(wl_bulk_sender_action_t) <= 256U,
               "sender actions exceed the stack budget");

static wl_bulk_sender_t sender;

static const wl_bulk_descriptor_t default_descriptor = {
    .transfer_id = 7U,
    .total_length = 13U,
    .requested_chunk_size = 5U,
    .object_crc32c = UINT32_C(0x12345678),
};

static void init_sender(uint32_t timeout_ms, uint32_t busy_retry_ms,
                        uint16_t max_retries) {
  const wl_bulk_sender_config_t config = {
      .status_timeout_ms = timeout_ms,
      .busy_retry_ms = busy_retry_ms,
      .max_retries = max_retries,
  };

  memset(&sender, 0, sizeof(sender));
  zassert_equal(wl_bulk_sender_init(&sender, &config), WL_BULK_OK);
}

static wl_bulk_sender_action_t acquire_action(wl_bulk_phase_t phase,
                                              uint64_t offset, size_t length) {
  wl_bulk_sender_action_t action;

  memset(&action, 0xa5, sizeof(action));
  zassert_equal(wl_bulk_sender_action_acquire(&sender, &action), WL_BULK_OK);
  zassert_equal(action.phase, phase);
  zassert_equal(action.descriptor.transfer_id, default_descriptor.transfer_id);
  zassert_equal(action.offset, offset);
  zassert_equal(action.length, length);
  zassert_not_equal(action.token, 0U);
  return action;
}

static wl_bulk_sender_action_t submit_action(wl_bulk_phase_t phase,
                                             uint64_t offset, size_t length,
                                             wl_time_ms_t now_ms) {
  wl_bulk_sender_action_t action = acquire_action(phase, offset, length);

  zassert_equal(wl_bulk_sender_action_submitted(&sender, &action, now_ms),
                WL_BULK_OK);
  return action;
}

static wl_bulk_status_t make_status(wl_bulk_phase_t phase,
                                    wl_bulk_status_code_t code,
                                    uint64_t next_offset,
                                    uint32_t accepted_chunk_size) {
  return (wl_bulk_status_t){
      .transfer_id = default_descriptor.transfer_id,
      .phase = phase,
      .code = code,
      .next_offset = next_offset,
      .accepted_chunk_size = accepted_chunk_size,
  };
}

static wl_bulk_sender_result_t get_result(void) {
  wl_bulk_sender_result_t result;

  memset(&result, 0xa5, sizeof(result));
  zassert_equal(wl_bulk_sender_get_result(&sender, &result), WL_BULK_OK);
  return result;
}

static void complete_abort(uint64_t next_offset, uint32_t accepted_chunk_size,
                           int32_t reason, wl_time_ms_t now_ms) {
  wl_bulk_status_t status;

  zassert_equal(wl_bulk_sender_request_abort(&sender, reason), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_ABORT, next_offset, 0U, now_ms);
  status = make_status(WL_BULK_PHASE_ABORT, WL_BULK_STATUS_ABORTED,
                       next_offset, accepted_chunk_size);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, now_ms + 1U),
                WL_BULK_OK);
  zassert_equal(get_result().state, WL_BULK_SENDER_ABORTED);
}

ZTEST(wirelink_bulk_sender, test_init_validation_and_action_tokens) {
  wl_bulk_sender_t uninitialized = {0};
  wl_bulk_sender_config_t config = {
      .status_timeout_ms = 10U,
      .busy_retry_ms = 2U,
      .max_retries = 1U,
  };
  wl_bulk_descriptor_t descriptor = default_descriptor;
  wl_bulk_sender_action_t first;
  wl_bulk_sender_action_t second;
  wl_bulk_sender_action_t changed;
  wl_bulk_status_t status;
  wl_bulk_sender_result_t result;
  wl_bulk_sender_stats_t stats;
  wl_bulk_deadline_hint_t hint;

  zassert_equal(wl_bulk_sender_init(NULL, &config), WL_BULK_ERR_INVALID_ARG);
  zassert_equal(wl_bulk_sender_init(&uninitialized, NULL),
                WL_BULK_ERR_INVALID_ARG);
  config.status_timeout_ms = 0U;
  zassert_equal(wl_bulk_sender_init(&uninitialized, &config),
                WL_BULK_ERR_INVALID_ARG);
  config.status_timeout_ms = UINT32_C(0x80000000);
  zassert_equal(wl_bulk_sender_init(&uninitialized, &config),
                WL_BULK_ERR_INVALID_ARG);
  config.status_timeout_ms = 10U;
  config.busy_retry_ms = 0U;
  zassert_equal(wl_bulk_sender_init(&uninitialized, &config),
                WL_BULK_ERR_INVALID_ARG);
  config.busy_retry_ms = UINT32_C(0x80000000);
  zassert_equal(wl_bulk_sender_init(&uninitialized, &config),
                WL_BULK_ERR_INVALID_ARG);

  zassert_equal(wl_bulk_sender_reset(NULL), WL_BULK_ERR_INVALID_ARG);
  zassert_equal(wl_bulk_sender_reset(&uninitialized),
                WL_BULK_ERR_NOT_INITIALIZED);
  zassert_equal(wl_bulk_sender_start(&uninitialized, &descriptor),
                WL_BULK_ERR_NOT_INITIALIZED);
  zassert_equal(wl_bulk_sender_poll(&uninitialized, 0U),
                WL_BULK_ERR_NOT_INITIALIZED);
  zassert_equal(wl_bulk_sender_get_result(&uninitialized, &result),
                WL_BULK_ERR_NOT_INITIALIZED);

  init_sender(10U, 2U, 1U);
  zassert_equal(wl_bulk_sender_get_result(&sender, NULL),
                WL_BULK_ERR_INVALID_ARG);
  zassert_equal(wl_bulk_sender_get_stats(&sender, NULL),
                WL_BULK_ERR_INVALID_ARG);
  zassert_equal(wl_bulk_sender_get_deadline_hint(&sender, 0U, NULL),
                WL_BULK_ERR_INVALID_ARG);
  zassert_equal(wl_bulk_sender_action_acquire(&sender, NULL),
                WL_BULK_ERR_INVALID_ARG);
  zassert_equal(wl_bulk_sender_action_acquire(&sender, &first),
                WL_BULK_ERR_NOT_FOUND);
  zassert_equal(wl_bulk_sender_get_deadline_hint(&sender, 0U, &hint),
                WL_BULK_OK);
  zassert_equal(hint.next_deadline_ms, WL_BULK_NO_DEADLINE_MS);

  descriptor.transfer_id = 0U;
  zassert_equal(wl_bulk_sender_start(&sender, &descriptor),
                WL_BULK_ERR_INVALID_ARG);
  descriptor = default_descriptor;
  descriptor.requested_chunk_size = 0U;
  zassert_equal(wl_bulk_sender_start(&sender, &descriptor),
                WL_BULK_ERR_INVALID_ARG);
  zassert_equal(wl_bulk_sender_start(&sender, NULL), WL_BULK_ERR_INVALID_ARG);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor),
                WL_BULK_ERR_BUSY);

  result = get_result();
  zassert_equal(result.state, WL_BULK_SENDER_READY);
  zassert_equal(result.status, WL_BULK_STATUS_OK);
  zassert_equal(result.next_offset, 0U);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_ERR_BUSY);
  first = acquire_action(WL_BULK_PHASE_BEGIN, 0U, 0U);
  zassert_equal(first.descriptor.total_length, default_descriptor.total_length);
  zassert_equal(first.descriptor.requested_chunk_size,
                default_descriptor.requested_chunk_size);
  zassert_equal(first.descriptor.object_crc32c,
                default_descriptor.object_crc32c);
  zassert_equal(wl_bulk_sender_action_acquire(&sender, &second),
                WL_BULK_ERR_BUSY);
  zassert_equal(wl_bulk_sender_request_abort(&sender, 91), WL_BULK_ERR_BUSY);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_ERR_BUSY);

  changed = first;
  changed.offset = 1U;
  zassert_equal(wl_bulk_sender_action_defer(&sender, &changed),
                WL_BULK_ERR_INVALID_ARG);
  zassert_equal(wl_bulk_sender_action_defer(&sender, &first), WL_BULK_OK);
  second = acquire_action(WL_BULK_PHASE_BEGIN, 0U, 0U);
  zassert_not_equal(first.token, second.token);
  zassert_equal(wl_bulk_sender_action_submitted(&sender, &first, 5U),
                WL_BULK_ERR_INVALID_STATE);
  zassert_equal(wl_bulk_sender_action_submitted(&sender, &second, 5U),
                WL_BULK_OK);
  zassert_equal(wl_bulk_sender_action_defer(&sender, &second),
                WL_BULK_ERR_INVALID_STATE);
  zassert_equal(wl_bulk_sender_action_acquire(&sender, &first),
                WL_BULK_ERR_NOT_FOUND);

  zassert_equal(wl_bulk_sender_get_stats(&sender, &stats), WL_BULK_OK);
  zassert_equal(stats.starts, 1U);
  zassert_equal(stats.actions_submitted, 1U);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_ERR_BUSY);
  status = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_INVALID, 0U, 0U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 6U), WL_BULK_OK);
  zassert_equal(get_result().state, WL_BULK_SENDER_FAILED);
  complete_abort(0U, 0U, 91, 7U);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_OK);
  zassert_equal(get_result().state, WL_BULK_SENDER_IDLE);
  zassert_equal(wl_bulk_sender_get_stats(&sender, &stats), WL_BULK_OK);
  zassert_equal(stats.starts, 1U);
  zassert_equal(stats.actions_submitted, 2U);
  zassert_equal(stats.statuses_received, 2U);
  zassert_equal(stats.failed, 1U);
  zassert_equal(stats.aborted, 1U);
}

ZTEST(wirelink_bulk_sender, test_resume_chunks_stale_status_and_completion) {
  wl_bulk_sender_action_t action;
  wl_bulk_status_t status;
  wl_bulk_sender_stats_t stats;
  wl_bulk_deadline_hint_t hint;
  wl_bulk_sender_result_t result;

  init_sender(10U, 3U, 2U);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  action = submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 100U);
  zassert_equal(wl_bulk_sender_get_deadline_hint(&sender, 105U, &hint),
                WL_BULK_OK);
  zassert_equal(hint.next_deadline_ms, 5U);

  status = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 5U, 4U);
  status.transfer_id++;
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 106U),
                WL_BULK_ERR_NOT_FOUND);
  status.transfer_id = default_descriptor.transfer_id;
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 106U), WL_BULK_OK);

  action = submit_action(WL_BULK_PHASE_CHUNK, 5U, 4U, 110U);
  status = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 5U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 111U),
                WL_BULK_ERR_NOT_FOUND);
  status = make_status(WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 9U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 111U), WL_BULK_OK);

  action = submit_action(WL_BULK_PHASE_CHUNK, 9U, 4U, 112U);
  status = make_status(WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 9U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 113U),
                WL_BULK_ERR_NOT_FOUND);
  result = get_result();
  zassert_equal(result.state, WL_BULK_SENDER_WAIT_STATUS);
  zassert_equal(result.next_offset, 9U);
  status.next_offset = 13U;
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 113U), WL_BULK_OK);

  action = submit_action(WL_BULK_PHASE_END, 13U, 0U, 114U);
  zassert_equal(action.descriptor.total_length, 13U);
  status = make_status(WL_BULK_PHASE_END, WL_BULK_STATUS_OK, 13U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 115U), WL_BULK_OK);
  result = get_result();
  zassert_equal(result.state, WL_BULK_SENDER_COMPLETED);
  zassert_equal(result.status, WL_BULK_STATUS_OK);
  zassert_equal(result.next_offset, 13U);
  zassert_equal(result.retry_count, 0U);
  zassert_equal(wl_bulk_sender_get_deadline_hint(&sender, 115U, &hint),
                WL_BULK_OK);
  zassert_equal(hint.next_deadline_ms, WL_BULK_NO_DEADLINE_MS);

  zassert_equal(wl_bulk_sender_get_stats(&sender, &stats), WL_BULK_OK);
  zassert_equal(stats.starts, 1U);
  zassert_equal(stats.actions_submitted, 4U);
  zassert_equal(stats.statuses_received, 4U);
  zassert_equal(stats.retries, 0U);
  zassert_equal(stats.protocol_errors, 0U);
  zassert_equal(stats.completed, 1U);
  zassert_equal(wl_bulk_sender_action_acquire(&sender, &action),
                WL_BULK_ERR_NOT_FOUND);
}

ZTEST(wirelink_bulk_sender, test_zero_length_object_runs_begin_then_end) {
  wl_bulk_descriptor_t descriptor = default_descriptor;
  wl_bulk_status_t status;

  descriptor.total_length = 0U;
  init_sender(10U, 2U, 1U);
  zassert_equal(wl_bulk_sender_start(&sender, &descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 5U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_END, 0U, 0U, 2U);
  status = make_status(WL_BULK_PHASE_END, WL_BULK_STATUS_OK, 0U, 5U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 3U), WL_BULK_OK);
  zassert_equal(get_result().state, WL_BULK_SENDER_COMPLETED);
}

ZTEST(wirelink_bulk_sender, test_timeout_retries_are_bounded_and_wrap_safe) {
  wl_bulk_sender_action_t first;
  wl_bulk_sender_action_t retry;
  wl_bulk_deadline_hint_t hint;
  wl_bulk_sender_result_t result;
  wl_bulk_sender_stats_t stats;
  const wl_time_ms_t started = UINT32_MAX - 4U;

  init_sender(10U, 3U, 2U);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  first = submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, started);
  zassert_equal(wl_bulk_sender_get_deadline_hint(&sender, 4U, &hint),
                WL_BULK_OK);
  zassert_equal(hint.next_deadline_ms, 1U);
  zassert_equal(wl_bulk_sender_poll(&sender, 4U), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_action_acquire(&sender, &retry),
                WL_BULK_ERR_NOT_FOUND);

  zassert_equal(wl_bulk_sender_poll(&sender, 5U), WL_BULK_OK);
  retry = acquire_action(WL_BULK_PHASE_BEGIN, 0U, 0U);
  zassert_not_equal(retry.token, first.token);
  result = get_result();
  zassert_equal(result.state, WL_BULK_SENDER_READY);
  zassert_equal(result.retry_count, 1U);
  zassert_equal(wl_bulk_sender_action_submitted(&sender, &retry, 5U),
                WL_BULK_OK);

  zassert_equal(wl_bulk_sender_poll(&sender, 15U), WL_BULK_OK);
  retry = acquire_action(WL_BULK_PHASE_BEGIN, 0U, 0U);
  zassert_equal(get_result().retry_count, 2U);
  zassert_equal(wl_bulk_sender_action_submitted(&sender, &retry, 15U),
                WL_BULK_OK);
  zassert_equal(wl_bulk_sender_poll(&sender, 25U), WL_BULK_OK);

  result = get_result();
  zassert_equal(result.state, WL_BULK_SENDER_FAILED);
  zassert_equal(result.status, WL_BULK_STATUS_TIMED_OUT);
  zassert_equal(result.retry_count, 2U);
  zassert_equal(wl_bulk_sender_get_stats(&sender, &stats), WL_BULK_OK);
  zassert_equal(stats.actions_submitted, 3U);
  zassert_equal(stats.retries, 2U);
  zassert_equal(stats.failed, 1U);
}

ZTEST(wirelink_bulk_sender, test_busy_uses_delay_and_retry_budget) {
  wl_bulk_status_t busy;
  wl_bulk_sender_action_t action;
  wl_bulk_deadline_hint_t hint;
  wl_bulk_sender_result_t result;
  wl_bulk_sender_stats_t stats;

  init_sender(50U, 5U, 2U);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 10U);
  busy = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_BUSY, 0U, 0U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &busy, 11U), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_get_deadline_hint(&sender, 11U, &hint),
                WL_BULK_OK);
  zassert_equal(hint.next_deadline_ms, 5U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &busy, 12U),
                WL_BULK_ERR_INVALID_STATE);
  zassert_equal(wl_bulk_sender_poll(&sender, 15U), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_action_acquire(&sender, &action),
                WL_BULK_ERR_NOT_FOUND);
  zassert_equal(wl_bulk_sender_poll(&sender, 16U), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 17U);

  zassert_equal(wl_bulk_sender_on_status(&sender, &busy, 18U), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_poll(&sender, 23U), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 24U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &busy, 25U), WL_BULK_OK);

  result = get_result();
  zassert_equal(result.state, WL_BULK_SENDER_FAILED);
  zassert_equal(result.status, WL_BULK_STATUS_BUSY);
  zassert_equal(result.retry_count, 2U);
  zassert_equal(wl_bulk_sender_get_stats(&sender, &stats), WL_BULK_OK);
  zassert_equal(stats.actions_submitted, 3U);
  zassert_equal(stats.statuses_received, 3U);
  zassert_equal(stats.busy_responses, 3U);
  zassert_equal(stats.retries, 2U);
  zassert_equal(stats.failed, 1U);
}

ZTEST(wirelink_bulk_sender, test_progress_and_negotiation_are_validated) {
  wl_bulk_status_t status;
  wl_bulk_sender_stats_t stats;

  init_sender(10U, 2U, 1U);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 0U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U),
                WL_BULK_ERR_PROTOCOL);
  zassert_equal(get_result().state, WL_BULK_SENDER_FAILED);

  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_ERR_BUSY);
  complete_abort(0U, 0U, 1, 2U);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 14U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U),
                WL_BULK_ERR_PROTOCOL);

  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_ERR_BUSY);
  complete_abort(0U, 0U, 2, 2U);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_CHUNK, 0U, 4U, 2U);
  status = make_status(WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 3U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 3U),
                WL_BULK_ERR_PROTOCOL);

  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_ERR_BUSY);
  complete_abort(0U, 4U, 3, 4U);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_CHUNK, 0U, 4U, 2U);
  status = make_status(WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OK, 4U, 3U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 3U),
                WL_BULK_ERR_PROTOCOL);

  zassert_equal(wl_bulk_sender_get_stats(&sender, &stats), WL_BULK_OK);
  zassert_equal(stats.protocol_errors, 4U);
  zassert_equal(stats.failed, 4U);
}

ZTEST(wirelink_bulk_sender, test_out_of_order_status_resynchronizes_progress) {
  wl_bulk_status_t status;
  wl_bulk_sender_action_t action;
  wl_bulk_sender_stats_t stats;

  init_sender(10U, 2U, 1U);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status =
      make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OUT_OF_ORDER, 4U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U), WL_BULK_OK);
  action = acquire_action(WL_BULK_PHASE_CHUNK, 4U, 4U);
  zassert_equal(wl_bulk_sender_action_submitted(&sender, &action, 2U),
                WL_BULK_OK);

  status =
      make_status(WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OUT_OF_ORDER, 8U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 3U), WL_BULK_OK);
  action = acquire_action(WL_BULK_PHASE_CHUNK, 8U, 4U);
  zassert_equal(get_result().next_offset, 8U);
  zassert_equal(wl_bulk_sender_action_defer(&sender, &action), WL_BULK_OK);

  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_ERR_BUSY);
  zassert_equal(wl_bulk_sender_request_abort(&sender, 1), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_ABORT, 8U, 0U, 4U);
  status = make_status(WL_BULK_PHASE_ABORT, WL_BULK_STATUS_ABORTED, 8U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 5U), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_CHUNK, 0U, 4U, 2U);
  status =
      make_status(WL_BULK_PHASE_CHUNK, WL_BULK_STATUS_OUT_OF_ORDER, 0U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 3U), WL_BULK_OK);
  action = acquire_action(WL_BULK_PHASE_CHUNK, 0U, 4U);
  zassert_equal(wl_bulk_sender_action_defer(&sender, &action), WL_BULK_OK);

  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_ERR_BUSY);
  zassert_equal(wl_bulk_sender_request_abort(&sender, 2), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_ABORT, 0U, 0U, 4U);
  status = make_status(WL_BULK_PHASE_ABORT, WL_BULK_STATUS_ABORTED, 0U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 5U), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status =
      make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OUT_OF_ORDER, 14U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U),
                WL_BULK_ERR_PROTOCOL);

  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_ERR_BUSY);
  complete_abort(0U, 0U, 3, 2U);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status =
      make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OUT_OF_ORDER, 4U, 0U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U),
                WL_BULK_ERR_PROTOCOL);

  zassert_equal(wl_bulk_sender_get_stats(&sender, &stats), WL_BULK_OK);
  zassert_equal(stats.protocol_errors, 2U);
  zassert_equal(stats.failed, 2U);
}

ZTEST(wirelink_bulk_sender, test_remote_abort_and_idle_expiry_are_terminal) {
  wl_bulk_status_t status;
  wl_bulk_sender_stats_t stats;

  init_sender(10U, 2U, 1U);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status = make_status(WL_BULK_PHASE_ABORT, WL_BULK_STATUS_TIMED_OUT, 4U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 20U), WL_BULK_OK);
  zassert_equal(get_result().state, WL_BULK_SENDER_FAILED);
  zassert_equal(get_result().status, WL_BULK_STATUS_TIMED_OUT);
  zassert_equal(get_result().next_offset, 4U);

  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 4U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_CHUNK, 4U, 4U, 2U);
  status = make_status(WL_BULK_PHASE_ABORT, WL_BULK_STATUS_ABORTED, 4U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 3U), WL_BULK_OK);
  zassert_equal(get_result().state, WL_BULK_SENDER_ABORTED);
  zassert_equal(get_result().next_offset, 4U);

  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status = make_status(WL_BULK_PHASE_ABORT, WL_BULK_STATUS_TIMED_OUT, 14U, 4U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U),
                WL_BULK_ERR_PROTOCOL);
  zassert_equal(get_result().state, WL_BULK_SENDER_FAILED);
  zassert_equal(get_result().status, WL_BULK_STATUS_INVALID);

  zassert_equal(wl_bulk_sender_get_stats(&sender, &stats), WL_BULK_OK);
  zassert_equal(stats.aborted, 1U);
  zassert_equal(stats.failed, 2U);
  zassert_equal(stats.protocol_errors, 1U);
}

ZTEST(wirelink_bulk_sender, test_abort_preempts_active_phase_and_is_terminal) {
  wl_bulk_sender_action_t begin;
  wl_bulk_sender_action_t abort_action;
  wl_bulk_status_t status;
  wl_bulk_sender_result_t result;
  wl_bulk_sender_stats_t stats;

  init_sender(10U, 2U, 1U);
  zassert_equal(wl_bulk_sender_request_abort(&sender, 1),
                WL_BULK_ERR_INVALID_STATE);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  begin = acquire_action(WL_BULK_PHASE_BEGIN, 0U, 0U);
  zassert_equal(wl_bulk_sender_request_abort(&sender, -9), WL_BULK_ERR_BUSY);
  zassert_equal(wl_bulk_sender_action_defer(&sender, &begin), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_request_abort(&sender, -9), WL_BULK_OK);
  abort_action = acquire_action(WL_BULK_PHASE_ABORT, 0U, 0U);
  zassert_equal(abort_action.abort_reason, -9);
  zassert_equal(wl_bulk_sender_action_submitted(&sender, &abort_action, 10U),
                WL_BULK_OK);

  status = make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_OK, 0U, 5U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 11U),
                WL_BULK_ERR_NOT_FOUND);
  status = make_status(WL_BULK_PHASE_ABORT, WL_BULK_STATUS_ABORTED, 0U, 0U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 12U), WL_BULK_OK);
  result = get_result();
  zassert_equal(result.state, WL_BULK_SENDER_ABORTED);
  zassert_equal(result.status, WL_BULK_STATUS_ABORTED);
  zassert_equal(wl_bulk_sender_get_stats(&sender, &stats), WL_BULK_OK);
  zassert_equal(stats.aborted, 1U);
  zassert_equal(stats.failed, 0U);
  zassert_equal(wl_bulk_sender_request_abort(&sender, 0),
                WL_BULK_ERR_INVALID_STATE);
}

ZTEST(wirelink_bulk_sender, test_remote_failure_and_future_phase_are_terminal) {
  wl_bulk_status_t status;
  wl_bulk_sender_stats_t stats;

  init_sender(10U, 2U, 1U);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status =
      make_status(WL_BULK_PHASE_BEGIN, WL_BULK_STATUS_WRITE_FAILED, 0U, 0U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U), WL_BULK_OK);
  zassert_equal(get_result().state, WL_BULK_SENDER_FAILED);
  zassert_equal(get_result().status, WL_BULK_STATUS_WRITE_FAILED);

  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_ERR_BUSY);
  complete_abort(0U, 0U, 1, 2U);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  status = make_status(WL_BULK_PHASE_END, WL_BULK_STATUS_OK, 13U, 5U);
  zassert_equal(wl_bulk_sender_on_status(&sender, &status, 1U),
                WL_BULK_ERR_PROTOCOL);
  zassert_equal(get_result().state, WL_BULK_SENDER_FAILED);

  zassert_equal(wl_bulk_sender_get_stats(&sender, &stats), WL_BULK_OK);
  zassert_equal(stats.failed, 2U);
  zassert_equal(stats.protocol_errors, 1U);
}

ZTEST(wirelink_bulk_sender,
      test_failed_transfer_requires_abort_attempt_before_force_reset) {
  wl_bulk_sender_result_t result;
  wl_bulk_sender_stats_t stats;

  init_sender(10U, 2U, 0U);
  zassert_equal(wl_bulk_sender_start(&sender, &default_descriptor), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_BEGIN, 0U, 0U, 0U);
  zassert_equal(wl_bulk_sender_poll(&sender, 10U), WL_BULK_OK);
  result = get_result();
  zassert_equal(result.state, WL_BULK_SENDER_FAILED);
  zassert_equal(result.status, WL_BULK_STATUS_TIMED_OUT);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_ERR_BUSY);

  zassert_equal(wl_bulk_sender_request_abort(&sender, 7), WL_BULK_OK);
  (void)submit_action(WL_BULK_PHASE_ABORT, 0U, 0U, 11U);
  zassert_equal(wl_bulk_sender_poll(&sender, 21U), WL_BULK_OK);
  result = get_result();
  zassert_equal(result.state, WL_BULK_SENDER_FAILED);
  zassert_equal(result.status, WL_BULK_STATUS_TIMED_OUT);
  zassert_equal(wl_bulk_sender_request_abort(&sender, 8),
                WL_BULK_ERR_INVALID_STATE);
  zassert_equal(wl_bulk_sender_reset(&sender), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_get_stats(&sender, &stats), WL_BULK_OK);
  zassert_equal(stats.failed, 2U);
}

ZTEST_SUITE(wirelink_bulk_sender, NULL, NULL, NULL, NULL, NULL);
