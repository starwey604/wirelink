/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/wirelink.h"

struct endpoint {
  wl_ctx_t ctx;
  wl_config_t config;
  wl_storage_t storage;
  uint8_t tx_payload[64];
  uint8_t tx_unit[WL_FRAME_MAX_COBS_LEN];
  uint8_t control_unit[WL_FRAME_MAX_COBS_LEN];
  uint8_t rx_fallback[WL_FRAME_MAX_COBS_LEN];
  uint8_t rx_fifo[WL_FRAME_MAX_COBS_LEN];
  uint8_t outbound[WL_FRAME_MAX_COBS_LEN];
  size_t outbound_len;
  size_t sink_calls;
};

static struct endpoint endpoint_left;
static struct endpoint endpoint_right;

static wl_sink_result_t memory_sink(void *user_data, wl_io_token_t token,
                                    const uint8_t *data, size_t len) {
  struct endpoint *endpoint = user_data;
  (void)token;
  endpoint->sink_calls++;
  if (endpoint->outbound_len != 0U || len > sizeof(endpoint->outbound)) {
    return WL_SINK_BUSY;
  }
  memcpy(endpoint->outbound, data, len);
  endpoint->outbound_len = len;
  return WL_SINK_SENT;
}

static void endpoint_init(struct endpoint *endpoint, wl_envelope_type_t envelope,
                          wl_integrity_t integrity, uint64_t session_id) {
  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->config = (wl_config_t){
      .max_payload_len = sizeof(endpoint->tx_payload),
      .envelope = envelope,
      .integrity = integrity,
      .session_id = session_id,
      .max_retries = 1U,
      .ack_timeout_ms = 5U,
      .max_transmission_unit = sizeof(endpoint->tx_unit),
  };
  endpoint->storage = (wl_storage_t){
      .tx_payload = endpoint->tx_payload,
      .tx_payload_size = sizeof(endpoint->tx_payload),
      .tx_unit = endpoint->tx_unit,
      .tx_unit_size = sizeof(endpoint->tx_unit),
      .control_unit = endpoint->control_unit,
      .control_unit_size = sizeof(endpoint->control_unit),
      .rx_fifo = endpoint->rx_fifo,
      .rx_fifo_size = sizeof(endpoint->rx_fifo),
      .rx_fallback = endpoint->rx_fallback,
      .rx_fallback_size = sizeof(endpoint->rx_fallback),
  };
  zassert_ok(wl_init(&endpoint->ctx, &endpoint->config, &endpoint->storage));
  zassert_ok(wl_set_sink(&endpoint->ctx, memory_sink, endpoint));
}

static void deliver(struct endpoint *from, struct endpoint *to) {
  size_t len = from->outbound_len;
  from->outbound_len = 0U;
  zassert_true(len != 0U);
  zassert_ok(wl_feed_unit(&to->ctx, from->outbound, len));
}

static void drop_outbound(struct endpoint *endpoint) {
  zassert_not_equal(endpoint->outbound_len, 0U);
  endpoint->outbound_len = 0U;
}

static wl_frame_view_t decode_outbound(const struct endpoint *endpoint) {
  wl_frame_view_t view = {0};

  zassert_not_equal(endpoint->outbound_len, 0U);
  zassert_ok(wl_frame_decode(endpoint->outbound, endpoint->outbound_len,
                             endpoint->config.integrity, &view));
  return view;
}

static void feed_ack(struct endpoint *endpoint, uint64_t session_id,
                     uint32_t sequence) {
  wl_wire_packet_t ack = {
      .type = WL_PACKET_ACK,
      .integrity = endpoint->config.integrity,
      .session_id = session_id,
      .sequence = sequence,
  };
  uint8_t wire[WL_FRAME_HEADER_SIZE + WL_FRAME_MAX_CRC];
  size_t wire_len = 0U;

  zassert_ok(wl_frame_encode(&ack, WL_ENVELOPE_NATIVE_PACKET, wire,
                             sizeof(wire), &wire_len));
  zassert_ok(wl_feed_unit(&endpoint->ctx, wire, wire_len));
}

static void feed_reliable_data(struct endpoint *endpoint, uint64_t session_id,
                               uint32_t sequence, uint16_t message_id,
                               const uint8_t *payload, size_t payload_len) {
  wl_wire_packet_t packet = {
      .type = WL_PACKET_DATA,
      .integrity = endpoint->config.integrity,
      .flags = WL_PACKET_FLAG_RELIABLE,
      .message_id = message_id,
      .session_id = session_id,
      .sequence = sequence,
      .payload = payload,
      .payload_len = payload_len,
  };
  uint8_t wire[WL_FRAME_HEADER_SIZE + sizeof(endpoint->tx_payload) +
               WL_FRAME_MAX_CRC];
  size_t wire_len = 0U;

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, wire,
                             sizeof(wire), &wire_len));
  zassert_ok(wl_feed_unit(&endpoint->ctx, wire, wire_len));
}

static void expect_reliable_rx(struct endpoint *endpoint, uint16_t message_id,
                               const uint8_t *payload, size_t payload_len,
                               wl_time_ms_t now_ms) {
  wl_event_t event = {0};

  zassert_ok(wl_poll(&endpoint->ctx, now_ms, &event));
  zassert_equal(event.type, WL_EVT_RELIABLE_RX);
  zassert_equal(event.message_id, message_id);
  zassert_equal(event.payload_len, payload_len);
  if (payload_len != 0U) {
    zassert_mem_equal(event.payload, payload, payload_len);
  }
  wl_event_release(&endpoint->ctx, &event);
}

ZTEST(wirelink_protocol_integration, test_reliable_round_trip)
{
  struct endpoint *left = &endpoint_left;
  struct endpoint *right = &endpoint_right;
  wl_tx_handle_t handle = 0U;
  wl_tx_result_t result = {0};
  wl_event_t event = {0};
  const uint8_t payload[] = {0x10U, 0x20U, 0x30U};

  endpoint_init(left, WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC32C, 0x11U);
  endpoint_init(right, WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC32C, 0x22U);
  zassert_ok(wl_send_reliable(&left->ctx, 7U, payload, sizeof(payload), &handle));
  deliver(left, right);
  deliver(right, left);
  zassert_ok(wl_poll(&right->ctx, 0U, &event));
  zassert_equal(event.type, WL_EVT_RELIABLE_RX);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&right->ctx, &event);
  zassert_ok(wl_poll(&left->ctx, 0U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  zassert_ok(wl_tx_take(&left->ctx, handle, &result));
  zassert_equal(result.state, WL_TX_STATE_SUCCESS);
}

ZTEST(wirelink_protocol_integration, test_retry_after_dropped_data)
{
  struct endpoint *left = &endpoint_left;
  struct endpoint *right = &endpoint_right;
  wl_event_t event = {0};
  wl_tx_handle_t handle = 0U;

  endpoint_init(left, WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC16, 0x33U);
  endpoint_init(right, WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC16, 0x44U);
  zassert_ok(wl_send_reliable(&left->ctx, 8U, NULL, 0U, &handle));
  left->outbound_len = 0U; /* Drop the initial DATA unit. */
  zassert_equal(wl_poll(&left->ctx, 6U, &event), WL_ERR_NO_DATA);
  deliver(left, right);
  deliver(right, left);
  zassert_ok(wl_poll(&right->ctx, 6U, &event));
  zassert_equal(event.type, WL_EVT_RELIABLE_RX);
  wl_event_release(&right->ctx, &event);
  zassert_ok(wl_poll(&left->ctx, 6U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
}

ZTEST(wirelink_protocol_integration,
      test_lost_ack_retries_and_receiver_reacks_duplicate)
{
  struct endpoint *left = &endpoint_left;
  struct endpoint *right = &endpoint_right;
  const uint8_t payload[] = {0xA1U, 0xB2U, 0xC3U, 0xD4U};
  uint8_t initial_data[WL_FRAME_HEADER_SIZE + sizeof(left->tx_payload) +
                       WL_FRAME_MAX_CRC];
  size_t initial_data_len;
  wl_tx_handle_t handle = 0U;
  wl_tx_result_t result = {0};
  wl_rx_counters_t counters = {0};
  wl_event_t event = {0};

  endpoint_init(left, WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC32C,
                UINT64_C(0x101));
  endpoint_init(right, WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC32C,
                UINT64_C(0x202));
  zassert_ok(
      wl_send_reliable(&left->ctx, 0x31U, payload, sizeof(payload), &handle));
  initial_data_len = left->outbound_len;
  zassert_true(initial_data_len <= sizeof(initial_data));
  memcpy(initial_data, left->outbound, initial_data_len);

  deliver(left, right);
  expect_reliable_rx(right, 0x31U, payload, sizeof(payload), 0U);
  drop_outbound(right); /* Lose the first ACK after accepting the DATA. */

  zassert_equal(wl_poll(&left->ctx, left->config.ack_timeout_ms, &event),
                WL_ERR_NO_DATA);
  zassert_equal(left->outbound_len, initial_data_len);
  zassert_mem_equal(left->outbound, initial_data, initial_data_len,
                    "a retry must preserve the original wire identity");
  deliver(left, right);

  zassert_equal(wl_poll(&right->ctx, left->config.ack_timeout_ms, &event),
                WL_ERR_NO_DATA,
                "a duplicate must not be delivered to the application");
  zassert_ok(wl_rx_get_counters(&right->ctx, &counters));
  zassert_equal(counters.duplicate, 1U);
  zassert_equal(right->sink_calls, 2U, "the duplicate must be re-ACKed");

  deliver(right, left);
  zassert_ok(wl_poll(&left->ctx, left->config.ack_timeout_ms, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  zassert_equal(event.handle, handle);
  zassert_ok(wl_tx_take(&left->ctx, handle, &result));
  zassert_equal(result.state, WL_TX_STATE_SUCCESS);
  zassert_equal(result.retries_used, 1U);
}

ZTEST(wirelink_protocol_integration,
      test_mismatched_and_stale_ack_are_ignored)
{
  struct endpoint *left = &endpoint_left;
  struct endpoint *right = &endpoint_right;
  wl_tx_handle_t first_handle = 0U;
  wl_tx_handle_t second_handle = 0U;
  wl_tx_result_t result = {0};
  wl_event_t event = {0};
  wl_frame_view_t first_data;
  wl_frame_view_t second_data;
  wl_tx_state_t state = WL_TX_STATE_IDLE;

  endpoint_init(left, WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC16,
                UINT64_C(0x303));
  endpoint_init(right, WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC16,
                UINT64_C(0x404));

  zassert_ok(wl_send_reliable(&left->ctx, 0x41U, NULL, 0U, &first_handle));
  first_data = decode_outbound(left);
  feed_ack(left, left->config.session_id ^ UINT64_C(0x100),
           first_data.sequence);
  feed_ack(left, left->config.session_id, first_data.sequence + 1U);
  zassert_equal(wl_poll(&left->ctx, 1U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_tx_status(&left->ctx, first_handle, &state));
  zassert_equal(state, WL_TX_STATE_WAITING_ACK);

  deliver(left, right);
  expect_reliable_rx(right, 0x41U, NULL, 0U, 1U);
  deliver(right, left);
  zassert_ok(wl_poll(&left->ctx, 1U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  zassert_ok(wl_tx_take(&left->ctx, first_handle, &result));

  zassert_ok(wl_send_reliable(&left->ctx, 0x42U, NULL, 0U, &second_handle));
  second_data = decode_outbound(left);
  zassert_not_equal(second_data.sequence, first_data.sequence);
  feed_ack(left, left->config.session_id, first_data.sequence);
  zassert_equal(wl_poll(&left->ctx, 2U, &event), WL_ERR_NO_DATA,
                "an ACK for the preceding transaction must be ignored");
  zassert_ok(wl_tx_status(&left->ctx, second_handle, &state));
  zassert_equal(state, WL_TX_STATE_WAITING_ACK);

  deliver(left, right);
  expect_reliable_rx(right, 0x42U, NULL, 0U, 2U);
  deliver(right, left);
  zassert_ok(wl_poll(&left->ctx, 2U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  zassert_equal(event.handle, second_handle);
  zassert_ok(wl_tx_take(&left->ctx, second_handle, &result));
}

ZTEST(wirelink_protocol_integration,
      test_dedup_key_and_most_recent_session_boundary)
{
  struct endpoint *receiver = &endpoint_right;
  const uint8_t payload_a[] = {0x11U};
  const uint8_t payload_b[] = {0x22U};
  const uint64_t session_a = UINT64_C(0xABC00001);
  const uint64_t session_b = UINT64_C(0xABC00002);
  wl_rx_counters_t counters = {0};
  wl_event_t event = {0};

  endpoint_init(receiver, WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC32C,
                UINT64_C(0x505));

  feed_reliable_data(receiver, session_a, 7U, 0x51U, payload_a,
                     sizeof(payload_a));
  expect_reliable_rx(receiver, 0x51U, payload_a, sizeof(payload_a), 0U);
  drop_outbound(receiver);

  feed_reliable_data(receiver, session_a, 7U, 0x51U, payload_a,
                     sizeof(payload_a));
  zassert_equal(wl_poll(&receiver->ctx, 1U, &event), WL_ERR_NO_DATA);
  drop_outbound(receiver);

  feed_reliable_data(receiver, session_b, 7U, 0x52U, payload_b,
                     sizeof(payload_b));
  expect_reliable_rx(receiver, 0x52U, payload_b, sizeof(payload_b), 2U);
  drop_outbound(receiver);

  feed_reliable_data(receiver, session_b, 8U, 0x53U, payload_a,
                     sizeof(payload_a));
  expect_reliable_rx(receiver, 0x53U, payload_a, sizeof(payload_a), 3U);
  drop_outbound(receiver);

  /* V1 intentionally remembers only the most recent (session, sequence). */
  feed_reliable_data(receiver, session_a, 7U, 0x54U, payload_b,
                     sizeof(payload_b));
  expect_reliable_rx(receiver, 0x54U, payload_b, sizeof(payload_b), 4U);
  drop_outbound(receiver);

  zassert_ok(wl_rx_get_counters(&receiver->ctx, &counters));
  zassert_equal(counters.duplicate, 1U);
  zassert_equal(receiver->sink_calls, 5U,
                "every reliable DATA, including a duplicate, needs an ACK");
}

static uint32_t next_prng(uint32_t *state) {
  uint32_t value = *state;

  value ^= value << 13U;
  value ^= value >> 17U;
  value ^= value << 5U;
  *state = value;
  return value;
}

ZTEST(wirelink_protocol_integration, test_seeded_reliable_fault_model)
{
  enum { ROUNDS = 32 };
  struct endpoint *left = &endpoint_left;
  struct endpoint *right = &endpoint_right;
  uint32_t seed = UINT32_C(0xC0FFEE12);
  wl_time_ms_t now_ms = UINT32_C(0xFFFFFFC0);
  size_t fault_counts[4] = {0};

  endpoint_init(left, WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC16,
                UINT64_C(0x606));
  endpoint_init(right, WL_ENVELOPE_NATIVE_PACKET, WL_INTEGRITY_CRC16,
                UINT64_C(0x707));
  zassert_equal(wl_poll(&left->ctx, now_ms, &(wl_event_t){0}), WL_ERR_NO_DATA);

  for (uint32_t round = 0U; round < ROUNDS; ++round) {
    uint32_t random = next_prng(&seed);
    uint32_t fault = random & 3U;
    uint8_t payload[] = {(uint8_t)(round >> 8U), (uint8_t)round,
                         (uint8_t)(random >> 8U), (uint8_t)random};
    wl_tx_handle_t handle = 0U;
    wl_tx_result_t result = {0};
    wl_event_t event = {0};
    wl_frame_view_t data;
    wl_tx_state_t state = WL_TX_STATE_IDLE;
    uint16_t expected_retries = 0U;

    fault_counts[fault]++;
    zassert_ok(wl_send_reliable(&left->ctx, (uint16_t)(0x600U + round), payload,
                                sizeof(payload), &handle));
    data = decode_outbound(left);

    if (fault == 3U) {
      feed_ack(left, left->config.session_id, data.sequence - 1U);
      zassert_equal(wl_poll(&left->ctx, now_ms, &event), WL_ERR_NO_DATA);
      zassert_ok(wl_tx_status(&left->ctx, handle, &state));
      zassert_equal(state, WL_TX_STATE_WAITING_ACK);
    }

    if (fault == 1U) {
      drop_outbound(left);
      now_ms += left->config.ack_timeout_ms;
      zassert_equal(wl_poll(&left->ctx, now_ms, &event), WL_ERR_NO_DATA);
      expected_retries = 1U;
    }

    deliver(left, right);
    expect_reliable_rx(right, (uint16_t)(0x600U + round), payload,
                       sizeof(payload), now_ms);

    if (fault == 2U) {
      drop_outbound(right);
      now_ms += left->config.ack_timeout_ms;
      zassert_equal(wl_poll(&left->ctx, now_ms, &event), WL_ERR_NO_DATA);
      deliver(left, right);
      zassert_equal(wl_poll(&right->ctx, now_ms, &event), WL_ERR_NO_DATA);
      expected_retries = 1U;
    }

    deliver(right, left);
    zassert_ok(wl_poll(&left->ctx, now_ms, &event));
    zassert_equal(event.type, WL_EVT_TX_SUCCESS);
    zassert_equal(event.handle, handle);
    zassert_ok(wl_tx_take(&left->ctx, handle, &result));
    zassert_equal(result.state, WL_TX_STATE_SUCCESS);
    zassert_equal(result.retries_used, expected_retries);
  }

  for (size_t i = 0U; i < ARRAY_SIZE(fault_counts); ++i) {
    zassert_not_equal(fault_counts[i], 0U,
                      "the fixed seed must exercise every fault class");
  }
}

ZTEST(wirelink_protocol_integration, test_cobs_chunking_and_corruption)
{
  struct endpoint *left = &endpoint_left;
  struct endpoint *right = &endpoint_right;
  wl_event_t event = {0};
  const uint8_t payload[] = {0x01U, 0x00U, 0x02U};

  endpoint_init(left, WL_ENVELOPE_COBS_STREAM, WL_INTEGRITY_CRC32C, 0x55U);
  endpoint_init(right, WL_ENVELOPE_COBS_STREAM, WL_INTEGRITY_CRC32C, 0x66U);
  zassert_ok(wl_send_unreliable(&left->ctx, 9U, payload, sizeof(payload)));
  for (size_t i = 0; i < left->outbound_len; ++i) {
    size_t accepted = 0U;
    zassert_ok(wl_feed_bytes(&right->ctx, &left->outbound[i], 1U,
                             &accepted));
    zassert_equal(accepted, 1U);
  }
  left->outbound_len = 0U;
  zassert_ok(wl_poll(&right->ctx, 0U, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&right->ctx, &event);
  zassert_ok(wl_poll(&left->ctx, 0U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);

  zassert_ok(wl_send_unreliable(&left->ctx, 9U, payload, sizeof(payload)));
  left->outbound[1] ^= 0x01U;
  size_t accepted = 0U;
  zassert_ok(wl_feed_bytes(&right->ctx, left->outbound, left->outbound_len,
                           &accepted));
  zassert_equal(accepted, left->outbound_len);
  zassert_equal(wl_poll(&right->ctx, 1U, &event), WL_ERR_NO_DATA);
}

static void expect_endpoint_hint(const struct endpoint *endpoint,
                                 wl_time_ms_t now_ms,
                                 uint32_t work_pending,
                                 uint32_t next_deadline_ms) {
  wl_poll_hint_t hint = {0};

  zassert_ok(wl_poll_get_hint(&endpoint->ctx, now_ms, &hint));
  zassert_equal(hint.work_pending, work_pending);
  zassert_equal(hint.next_deadline_ms, next_deadline_ms);
}

ZTEST(wirelink_protocol_integration,
      test_hint_drives_cobs_retry_and_round_trip_without_idle_polling)
{
  struct endpoint *left = &endpoint_left;
  struct endpoint *right = &endpoint_right;
  const wl_time_ms_t start_ms = UINT32_MAX - 2U;
  const wl_time_ms_t retry_ms = 2U;
  const uint8_t payload[] = {0x01U, 0x00U, 0x02U};
  wl_tx_handle_t handle = 0U;
  wl_tx_result_t result = {0};
  wl_event_t event = {0};

  endpoint_init(left, WL_ENVELOPE_COBS_STREAM, WL_INTEGRITY_CRC16,
                UINT64_C(0x808));
  endpoint_init(right, WL_ENVELOPE_COBS_STREAM, WL_INTEGRITY_CRC16,
                UINT64_C(0x909));
  zassert_equal(wl_poll(&left->ctx, start_ms, &event), WL_ERR_NO_DATA);
  zassert_ok(
      wl_send_reliable(&left->ctx, 0x71U, payload, sizeof(payload), &handle));
  drop_outbound(left);

  expect_endpoint_hint(left, start_ms, 0U, left->config.ack_timeout_ms);
  expect_endpoint_hint(left, 1U, 0U, 1U);
  expect_endpoint_hint(left, retry_ms, 1U, 0U);
  zassert_equal(wl_poll(&left->ctx, retry_ms, &event), WL_ERR_NO_DATA);
  zassert_not_equal(left->outbound_len, 0U);

  deliver(left, right);
  expect_endpoint_hint(right, retry_ms, 1U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_poll(&right->ctx, retry_ms, &event));
  zassert_equal(event.type, WL_EVT_RELIABLE_RX);
  zassert_equal(event.message_id, 0x71U);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&right->ctx, &event);

  deliver(right, left);
  expect_endpoint_hint(left, retry_ms, 1U, left->config.ack_timeout_ms);
  zassert_ok(wl_poll(&left->ctx, retry_ms, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  zassert_equal(event.handle, handle);
  expect_endpoint_hint(left, retry_ms, 0U, WL_POLL_NO_DEADLINE_MS);
  zassert_ok(wl_tx_take(&left->ctx, handle, &result));
  zassert_equal(result.state, WL_TX_STATE_SUCCESS);
  zassert_equal(result.retries_used, 1U);
}

ZTEST_SUITE(wirelink_protocol_integration, NULL, NULL, NULL, NULL, NULL);
