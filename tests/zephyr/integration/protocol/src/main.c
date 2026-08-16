/* SPDX-License-Identifier: Apache-2.0 */

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
};

static struct endpoint endpoint_left;
static struct endpoint endpoint_right;

static wl_sink_result_t memory_sink(void *user_data, wl_io_token_t token,
                                    const uint8_t *data, size_t len) {
  struct endpoint *endpoint = user_data;
  (void)token;
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

ZTEST_SUITE(wirelink_protocol_integration, NULL, NULL, NULL, NULL, NULL);
