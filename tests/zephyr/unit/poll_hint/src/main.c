/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/wirelink.h"

struct sink_capture {
  wl_sink_result_t script[8];
  size_t script_length;
  size_t script_index;
  size_t call_count;
  wl_io_token_t last_token;
};

struct fixture {
  wl_ctx_t ctx;
  struct sink_capture sink;
  wl_config_t config;
  uint8_t tx_payload[64];
  uint8_t tx_unit[256];
  uint8_t control_unit[64];
  uint8_t rx_fifo[256];
  uint8_t rx_fallback[256];
  uint8_t unit_slots[2][128];
};

static struct fixture test_fixture;

static wl_sink_result_t scripted_sink(void *user_data, wl_io_token_t token,
                                      const uint8_t *data, size_t length) {
  struct sink_capture *capture = user_data;

  (void)data;
  (void)length;
  capture->last_token = token;
  capture->call_count++;
  if (capture->script_index < capture->script_length) {
    return capture->script[capture->script_index++];
  }
  return WL_SINK_FAILED;
}

static struct fixture *fixture_init(wl_envelope_type_t envelope,
                                    uint32_t ack_timeout_ms,
                                    uint16_t max_retries,
                                    const wl_sink_result_t *script,
                                    size_t script_length) {
  struct fixture *fixture = &test_fixture;
  wl_storage_t storage;

  zassert_true(script_length <= ARRAY_SIZE(fixture->sink.script));
  memset(fixture, 0, sizeof(*fixture));
  fixture->config = (wl_config_t){
      .max_payload_len = sizeof(fixture->tx_payload),
      .envelope = envelope,
      .integrity = WL_INTEGRITY_NONE,
      .session_id = UINT64_C(0x504f4c4c48494e54),
      .max_retries = max_retries,
      .ack_timeout_ms = ack_timeout_ms,
      .max_transmission_unit = sizeof(fixture->tx_unit),
  };
  storage = (wl_storage_t){
      .tx_payload = fixture->tx_payload,
      .tx_payload_size = sizeof(fixture->tx_payload),
      .tx_unit = fixture->tx_unit,
      .tx_unit_size = sizeof(fixture->tx_unit),
      .control_unit = fixture->control_unit,
      .control_unit_size = sizeof(fixture->control_unit),
      .rx_fifo = fixture->rx_fifo,
      .rx_fifo_size = sizeof(fixture->rx_fifo),
      .rx_fallback = fixture->rx_fallback,
      .rx_fallback_size = sizeof(fixture->rx_fallback),
  };
  if (script_length != 0U) {
    memcpy(fixture->sink.script, script, script_length * sizeof(script[0]));
  }
  fixture->sink.script_length = script_length;
  zassert_ok(wl_init(&fixture->ctx, &fixture->config, &storage));
  zassert_ok(wl_set_sink(&fixture->ctx, scripted_sink, &fixture->sink));
  return fixture;
}

static void expect_hint(const wl_ctx_t *ctx, wl_time_ms_t now_ms,
                        uint32_t work_pending, uint32_t next_deadline_ms) {
  wl_poll_hint_t hint = {UINT32_MAX, 0U};

  zassert_ok(wl_poll_get_hint(ctx, now_ms, &hint));
  zassert_equal(hint.work_pending, work_pending);
  zassert_equal(hint.next_deadline_ms, next_deadline_ms);
}

static size_t encode_packet(const struct fixture *fixture,
                            wl_envelope_type_t envelope, uint8_t flags,
                            uint16_t message_id, uint64_t session_id,
                            uint32_t sequence, const uint8_t *payload,
                            size_t payload_length, uint8_t *output,
                            size_t output_size) {
  const wl_wire_packet_t packet = {
      .type = WL_PACKET_DATA,
      .integrity = fixture->config.integrity,
      .flags = flags,
      .message_id = message_id,
      .session_id = session_id,
      .sequence = sequence,
      .payload = payload,
      .payload_len = payload_length,
  };
  size_t encoded_length = 0U;

  zassert_ok(
      wl_frame_encode(&packet, envelope, output, output_size, &encoded_length));
  return encoded_length;
}

ZTEST(wirelink_poll_hint_unit, test_query_validation_and_idle_state) {
  wl_ctx_t uninitialized = {0};
  wl_poll_hint_t hint = {7U, 9U};
  struct fixture *fixture;

  zassert_equal(wl_poll_get_hint(NULL, 0U, &hint), WL_ERR_INVALID_ARG);
  zassert_equal(hint.work_pending, 0U);
  zassert_equal(hint.next_deadline_ms, WL_POLL_NO_DEADLINE_MS);
  hint = (wl_poll_hint_t){7U, 9U};
  zassert_equal(wl_poll_get_hint(&uninitialized, 0U, &hint),
                WL_ERR_NOT_INITIALIZED);
  zassert_equal(hint.work_pending, 0U);
  zassert_equal(hint.next_deadline_ms, WL_POLL_NO_DEADLINE_MS);
  zassert_equal(wl_poll_get_hint(&uninitialized, 0U, NULL), WL_ERR_INVALID_ARG);

  fixture = fixture_init(WL_ENVELOPE_NATIVE_PACKET, 5U, 1U, NULL, 0U);
  expect_hint(&fixture->ctx, 0U, 0U, WL_POLL_NO_DEADLINE_MS);
}

ZTEST(wirelink_poll_hint_unit, test_async_tx_completion_becomes_poll_work) {
  const wl_sink_result_t script[] = {WL_SINK_STARTED};
  struct fixture *fixture = fixture_init(WL_ENVELOPE_NATIVE_PACKET, 5U, 0U,
                                         script, ARRAY_SIZE(script));
  wl_event_t event = {0};

  zassert_equal(wl_poll(&fixture->ctx, 100U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_send_unreliable(&fixture->ctx, 1U, NULL, 0U));
  expect_hint(&fixture->ctx, 100U, 0U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_tx_complete(&fixture->ctx, fixture->sink.last_token, WL_OK));
  expect_hint(&fixture->ctx, 100U, 1U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_poll(&fixture->ctx, 100U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  expect_hint(&fixture->ctx, 100U, 0U, WL_POLL_NO_DEADLINE_MS);
}

ZTEST(wirelink_poll_hint_unit, test_reliable_tx_completion_arms_deadline) {
  const wl_sink_result_t script[] = {WL_SINK_STARTED};
  struct fixture *fixture = fixture_init(WL_ENVELOPE_NATIVE_PACKET, 5U, 0U,
                                         script, ARRAY_SIZE(script));
  wl_tx_handle_t handle = 0U;
  wl_event_t event = {0};

  zassert_equal(wl_poll(&fixture->ctx, 100U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_send_reliable(&fixture->ctx, 2U, NULL, 0U, &handle));
  expect_hint(&fixture->ctx, 100U, 0U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_tx_complete(&fixture->ctx, fixture->sink.last_token, WL_OK));
  expect_hint(&fixture->ctx, 100U, 0U, 5U);
  expect_hint(&fixture->ctx, 105U, 1U, 0U);
}

ZTEST(wirelink_poll_hint_unit,
      test_ack_deadline_retry_exhaustion_and_uint32_wrap) {
  const wl_sink_result_t script[] = {WL_SINK_SENT, WL_SINK_SENT};
  struct fixture *fixture = fixture_init(WL_ENVELOPE_NATIVE_PACKET, 5U, 1U,
                                         script, ARRAY_SIZE(script));
  const wl_time_ms_t before_wrap = UINT32_MAX - 2U;
  wl_tx_handle_t handle = 0U;
  wl_event_t event = {0};

  zassert_equal(wl_poll(&fixture->ctx, before_wrap, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_send_reliable(&fixture->ctx, 2U, NULL, 0U, &handle));
  expect_hint(&fixture->ctx, before_wrap, 0U, 5U);
  expect_hint(&fixture->ctx, 1U, 0U, 1U);
  expect_hint(&fixture->ctx, 2U, 1U, 0U);
  zassert_equal(fixture->sink.call_count, 1U,
                "the query must not execute protocol work");

  zassert_equal(wl_poll(&fixture->ctx, 2U, &event), WL_ERR_NO_DATA);
  zassert_equal(fixture->sink.call_count, 2U);
  expect_hint(&fixture->ctx, 6U, 0U, 1U);
  expect_hint(&fixture->ctx, 7U, 1U, 0U);
  zassert_ok(wl_poll(&fixture->ctx, 7U, &event));
  zassert_equal(event.type, WL_EVT_TX_TIMEOUT);
  zassert_equal(event.handle, handle);
  expect_hint(&fixture->ctx, 7U, 0U, WL_POLL_NO_DEADLINE_MS);
}

ZTEST(wirelink_poll_hint_unit, test_zero_ack_timeout_has_no_deadline) {
  const wl_sink_result_t script[] = {WL_SINK_SENT};
  struct fixture *fixture = fixture_init(WL_ENVELOPE_NATIVE_PACKET, 0U, 1U,
                                         script, ARRAY_SIZE(script));
  wl_tx_handle_t handle = 0U;

  zassert_ok(wl_send_reliable(&fixture->ctx, 3U, NULL, 0U, &handle));
  expect_hint(&fixture->ctx, UINT32_MAX, 0U, WL_POLL_NO_DEADLINE_MS);
}

ZTEST(wirelink_poll_hint_unit,
      test_cobs_complete_frame_and_lease_gate_consumer_work) {
  struct fixture *fixture =
      fixture_init(WL_ENVELOPE_COBS_STREAM, 5U, 0U, NULL, 0U);
  const uint8_t payload[] = {0x10U, 0x00U, 0x20U};
  uint8_t first_unit[128];
  uint8_t second_unit[128];
  size_t first_length;
  size_t second_length;
  size_t accepted = 0U;
  wl_event_t event = {0};

  first_length =
      encode_packet(fixture, WL_ENVELOPE_COBS_STREAM, 0U, 0x10U, 0U, 0U,
                    payload, sizeof(payload), first_unit, sizeof(first_unit));
  second_length =
      encode_packet(fixture, WL_ENVELOPE_COBS_STREAM, 0U, 0x11U, 0U, 0U,
                    payload, sizeof(payload), second_unit, sizeof(second_unit));
  zassert_true(first_length > 1U);
  zassert_equal(first_unit[first_length - 1U], 0U);

  zassert_ok(
      wl_feed_bytes(&fixture->ctx, first_unit, first_length - 1U, &accepted));
  zassert_equal(accepted, first_length - 1U);
  expect_hint(&fixture->ctx, 0U, 0U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_feed_bytes(&fixture->ctx, &first_unit[first_length - 1U], 1U,
                           &accepted));
  expect_hint(&fixture->ctx, 0U, 1U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_poll(&fixture->ctx, 0U, &event));
  zassert_equal(event.message_id, 0x10U);

  zassert_ok(
      wl_feed_bytes(&fixture->ctx, second_unit, second_length, &accepted));
  expect_hint(&fixture->ctx, 0U, 0U, WL_POLL_NO_DEADLINE_MS);
  wl_event_release(&fixture->ctx, &event);
  expect_hint(&fixture->ctx, 0U, 1U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_poll(&fixture->ctx, 0U, &event));
  zassert_equal(event.message_id, 0x11U);
  wl_event_release(&fixture->ctx, &event);
  expect_hint(&fixture->ctx, 0U, 0U, WL_POLL_NO_DEADLINE_MS);
}

ZTEST(wirelink_poll_hint_unit, test_unit_queue_is_immediate_until_leased) {
  struct fixture *fixture =
      fixture_init(WL_ENVELOPE_NATIVE_PACKET, 5U, 0U, NULL, 0U);
  const wl_rx_unit_queue_config_t queue_config = {
      .storage = &fixture->unit_slots[0][0],
      .storage_size = sizeof(fixture->unit_slots),
      .unit_size = sizeof(fixture->unit_slots[0]),
      .slot_count = ARRAY_SIZE(fixture->unit_slots),
  };
  wl_rx_unit_claim_t first = {0};
  wl_rx_unit_claim_t second = {0};
  wl_event_t event = {0};
  size_t encoded_length;

  zassert_ok(wl_rx_unit_queue_init(&fixture->ctx, &queue_config));
  zassert_ok(
      wl_rx_unit_claim(&fixture->ctx, sizeof(fixture->unit_slots[0]), &first));
  encoded_length =
      encode_packet(fixture, WL_ENVELOPE_NATIVE_PACKET, 0U, 0x20U, 0U, 0U, NULL,
                    0U, first.span.data, first.span.length);
  zassert_ok(wl_rx_unit_commit(&fixture->ctx, &first, encoded_length));
  zassert_ok(
      wl_rx_unit_claim(&fixture->ctx, sizeof(fixture->unit_slots[0]), &second));
  encoded_length =
      encode_packet(fixture, WL_ENVELOPE_NATIVE_PACKET, 0U, 0x21U, 0U, 0U, NULL,
                    0U, second.span.data, second.span.length);
  zassert_ok(wl_rx_unit_commit(&fixture->ctx, &second, encoded_length));

  expect_hint(&fixture->ctx, 0U, 1U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_poll(&fixture->ctx, 0U, &event));
  zassert_equal(event.message_id, 0x20U);
  expect_hint(&fixture->ctx, 0U, 0U, WL_POLL_NO_DEADLINE_MS);
  wl_event_release(&fixture->ctx, &event);
  expect_hint(&fixture->ctx, 0U, 1U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_poll(&fixture->ctx, 0U, &event));
  zassert_equal(event.message_id, 0x21U);
  wl_event_release(&fixture->ctx, &event);
  expect_hint(&fixture->ctx, 0U, 0U, WL_POLL_NO_DEADLINE_MS);
}

ZTEST(wirelink_poll_hint_unit, test_overflow_notification_is_consumer_work) {
  struct fixture *fixture =
      fixture_init(WL_ENVELOPE_COBS_STREAM, 5U, 0U, NULL, 0U);
  wl_rx_counters_t counters = {0};
  wl_event_t event = {0};

  wl_rx_note_overflow(&fixture->ctx);
  expect_hint(&fixture->ctx, 0U, 1U, WL_POLL_NO_DEADLINE_MS);
  zassert_equal(wl_poll(&fixture->ctx, 0U, &event), WL_ERR_NO_DATA);
  expect_hint(&fixture->ctx, 0U, 0U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_rx_get_counters(&fixture->ctx, &counters));
  zassert_equal(counters.overflow, 1U);
}

ZTEST(wirelink_poll_hint_unit,
      test_busy_data_waits_for_external_writable_notification) {
  const wl_sink_result_t script[] = {WL_SINK_BUSY, WL_SINK_SENT};
  struct fixture *fixture = fixture_init(WL_ENVELOPE_NATIVE_PACKET, 5U, 0U,
                                         script, ARRAY_SIZE(script));
  wl_event_t event = {0};

  zassert_ok(wl_send_unreliable(&fixture->ctx, 4U, NULL, 0U));
  expect_hint(&fixture->ctx, 0U, 0U, WL_POLL_NO_DEADLINE_MS);
  expect_hint(&fixture->ctx, 10U, 0U, WL_POLL_NO_DEADLINE_MS);
  zassert_equal(fixture->sink.call_count, 1U,
                "a BUSY sink must not create a zero-delay spin loop");

  /* Simulate a wake after the sink becomes writable. */
  zassert_ok(wl_poll(&fixture->ctx, 10U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  zassert_equal(fixture->sink.call_count, 2U);
  expect_hint(&fixture->ctx, 10U, 0U, WL_POLL_NO_DEADLINE_MS);
}

ZTEST(wirelink_poll_hint_unit,
      test_busy_control_waits_after_current_event_is_drained) {
  const wl_sink_result_t script[] = {WL_SINK_BUSY, WL_SINK_BUSY, WL_SINK_SENT};
  struct fixture *fixture = fixture_init(WL_ENVELOPE_NATIVE_PACKET, 5U, 0U,
                                         script, ARRAY_SIZE(script));
  uint8_t reliable_data[64];
  size_t reliable_length;
  wl_event_t event = {0};

  reliable_length =
      encode_packet(fixture, WL_ENVELOPE_NATIVE_PACKET, WL_PACKET_FLAG_RELIABLE,
                    0x30U, UINT64_C(0x1122334455667788), 7U, NULL, 0U,
                    reliable_data, sizeof(reliable_data));
  zassert_ok(wl_feed_unit(&fixture->ctx, reliable_data, reliable_length));
  expect_hint(&fixture->ctx, 0U, 1U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_poll(&fixture->ctx, 0U, &event));
  zassert_equal(event.type, WL_EVT_RELIABLE_RX);
  zassert_equal(fixture->sink.call_count, 2U);
  expect_hint(&fixture->ctx, 0U, 0U, WL_POLL_NO_DEADLINE_MS);
  wl_event_release(&fixture->ctx, &event);
  expect_hint(&fixture->ctx, 0U, 0U, WL_POLL_NO_DEADLINE_MS);

  /* A writable/activity wake causes one new poll attempt. */
  zassert_equal(wl_poll(&fixture->ctx, 1U, &event), WL_ERR_NO_DATA);
  zassert_equal(fixture->sink.call_count, 3U);
  expect_hint(&fixture->ctx, 1U, 0U, WL_POLL_NO_DEADLINE_MS);
}

ZTEST_SUITE(wirelink_poll_hint_unit, NULL, NULL, NULL, NULL, NULL);
