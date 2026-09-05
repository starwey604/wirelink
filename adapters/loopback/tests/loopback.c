/* SPDX-License-Identifier: Apache-2.0 */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "wirelink/loopback.h"

#define TEST_MAX_PAYLOAD 32U
#define TEST_UNIT_SIZE 64U

typedef struct endpoint {
  wl_ctx_t link;
  uint8_t tx_payload[TEST_MAX_PAYLOAD];
  uint8_t tx_unit[TEST_UNIT_SIZE];
  uint8_t control_unit[TEST_UNIT_SIZE];
  uint8_t rx_fallback[TEST_UNIT_SIZE];
} endpoint_t;

static void endpoint_init(endpoint_t *endpoint, uint64_t session_id,
                          wl_integrity_t integrity) {
  const wl_config_t config = {
      .max_payload_len = TEST_MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_NATIVE_PACKET,
      .integrity = integrity,
      .session_id = session_id,
      .max_retries = 1U,
      .ack_timeout_ms = 10U,
      .max_transmission_unit = TEST_UNIT_SIZE,
  };
  const wl_storage_t storage = {
      .tx_payload = endpoint->tx_payload,
      .tx_payload_size = sizeof(endpoint->tx_payload),
      .tx_unit = endpoint->tx_unit,
      .tx_unit_size = sizeof(endpoint->tx_unit),
      .control_unit = endpoint->control_unit,
      .control_unit_size = sizeof(endpoint->control_unit),
      .rx_fallback = endpoint->rx_fallback,
      .rx_fallback_size = sizeof(endpoint->rx_fallback),
  };

  memset(endpoint, 0, sizeof(*endpoint));
  assert(wl_init(&endpoint->link, &config, &storage) == WL_OK);
}

static wl_event_t poll_event(wl_ctx_t *link, wl_event_type_t type) {
  wl_event_t event;

  memset(&event, 0, sizeof(event));
  assert(wl_poll(link, 1U, &event) == WL_OK);
  assert(event.type == type);
  return event;
}

static void drain_unreliable_completion(wl_ctx_t *link) {
  const wl_event_t event = poll_event(link, WL_EVT_TX_SUCCESS);
  assert(event.handle == 0U);
}

int main(void) {
  endpoint_t endpoint_a;
  endpoint_t endpoint_b;
  endpoint_t incompatible;
  wl_loopback_t loopback = {0};
  wl_loopback_service_result_t service;
  wl_adapter_stats_t stats;
  wl_event_t event_a;
  wl_event_t event_b;
  wl_tx_result_t tx_result;
  wl_tx_handle_t handle;
  const uint8_t first[] = {1U, 2U, 3U};
  const uint8_t second[] = {4U, 5U};

  endpoint_init(&endpoint_a, UINT64_C(0x1001), WL_INTEGRITY_CRC32C);
  endpoint_init(&endpoint_b, UINT64_C(0x2002), WL_INTEGRITY_CRC32C);
  endpoint_init(&incompatible, UINT64_C(0x3003), WL_INTEGRITY_NONE);
  assert(wl_loopback_init(&loopback, &endpoint_a.link,
                          &incompatible.link) == WL_ERR_INVALID_ARG);
  assert(wl_loopback_init(&loopback, &endpoint_a.link,
                          &endpoint_b.link) == WL_OK);
  assert(wl_loopback_service(&loopback, 1U, &service) == WL_ERR_NO_DATA);
  assert(service.attempts == 0U && service.delivered == 0U);

  assert(wl_send_unreliable(&endpoint_a.link, 0x10U, first,
                            sizeof(first)) == WL_OK);
  memset(&event_a, 0, sizeof(event_a));
  assert(wl_poll(&endpoint_a.link, 0U, &event_a) == WL_ERR_NO_DATA);
  assert(wl_loopback_get_stats(&loopback, WL_LOOPBACK_ENDPOINT_A, &stats) ==
         WL_OK);
  assert(stats.started == 1U && stats.tx_active == 1U &&
         stats.tx_units == 1U);
  assert(wl_loopback_service(&loopback, 1U, &service) == WL_OK);
  assert(service.attempts == 1U && service.delivered == 1U &&
         service.blocked == 0U);
  event_b = poll_event(&endpoint_b.link, WL_EVT_UNRELIABLE_RX);
  assert(event_b.message_id == 0x10U && event_b.payload_len == sizeof(first));
  assert(memcmp(event_b.payload, first, sizeof(first)) == 0);
  drain_unreliable_completion(&endpoint_a.link);

  assert(wl_send_unreliable(&endpoint_a.link, 0x11U, second,
                            sizeof(second)) == WL_OK);
  assert(wl_loopback_service(&loopback, 2U, &service) ==
         WL_ERR_WOULD_BLOCK);
  assert(service.attempts == 1U && service.delivered == 0U &&
         service.blocked == 1U);
  assert(wl_loopback_get_stats(&loopback, WL_LOOPBACK_ENDPOINT_B, &stats) ==
         WL_OK);
  assert(stats.rx_backpressure == 1U && stats.rx_paused == 1U &&
         stats.rx_units == 1U && stats.rx_bytes > sizeof(first));
  wl_event_release(&endpoint_b.link, &event_b);
  assert(wl_loopback_service(&loopback, 1U, &service) == WL_OK);
  event_b = poll_event(&endpoint_b.link, WL_EVT_UNRELIABLE_RX);
  assert(event_b.message_id == 0x11U);
  wl_event_release(&endpoint_b.link, &event_b);
  drain_unreliable_completion(&endpoint_a.link);

  assert(wl_send_unreliable(&endpoint_a.link, 0x20U, first,
                            sizeof(first)) == WL_OK);
  assert(wl_send_unreliable(&endpoint_b.link, 0x21U, second,
                            sizeof(second)) == WL_OK);
  assert(wl_loopback_service(&loopback, 2U, &service) == WL_OK);
  assert(service.delivered == 2U);
  event_a = poll_event(&endpoint_a.link, WL_EVT_UNRELIABLE_RX);
  event_b = poll_event(&endpoint_b.link, WL_EVT_UNRELIABLE_RX);
  assert(event_a.message_id == 0x21U && event_b.message_id == 0x20U);
  wl_event_release(&endpoint_a.link, &event_a);
  wl_event_release(&endpoint_b.link, &event_b);
  drain_unreliable_completion(&endpoint_a.link);
  drain_unreliable_completion(&endpoint_b.link);

  assert(wl_send_reliable(&endpoint_a.link, 0x30U, first, sizeof(first),
                          &handle) == WL_OK);
  assert(wl_loopback_service(&loopback, 4U, &service) == WL_OK);
  assert(service.delivered == 2U);
  event_b = poll_event(&endpoint_b.link, WL_EVT_RELIABLE_RX);
  assert(event_b.message_id == 0x30U);
  wl_event_release(&endpoint_b.link, &event_b);
  event_a = poll_event(&endpoint_a.link, WL_EVT_TX_SUCCESS);
  assert(event_a.handle == handle);
  assert(wl_tx_take(&endpoint_a.link, handle, &tx_result) == WL_OK);
  assert(tx_result.state == WL_TX_STATE_SUCCESS);

  assert(wl_loopback_reset_stats(&loopback) == WL_OK);
  assert(wl_loopback_get_stats(&loopback, WL_LOOPBACK_ENDPOINT_A, &stats) ==
         WL_OK);
  assert(stats.started == 1U && stats.tx_units == 0U);

  assert(wl_send_reliable(&endpoint_a.link, 0x31U, second, sizeof(second),
                          &handle) == WL_OK);
  wl_loopback_quiesce(&loopback);
  assert(wl_loopback_get_stats(&loopback, WL_LOOPBACK_ENDPOINT_A, &stats) ==
         WL_OK);
  assert(stats.started == 0U && stats.tx_active == 0U && stats.errors == 1U);
  event_a = poll_event(&endpoint_a.link, WL_EVT_TX_FAILED);
  assert(event_a.handle == handle);
  assert(wl_tx_take(&endpoint_a.link, handle, &tx_result) == WL_OK);
  assert(tx_result.state == WL_TX_STATE_FAILED);
  assert(wl_loopback_service(&loopback, 1U, &service) ==
         WL_ERR_INVALID_STATE);
  wl_loopback_quiesce(&loopback);
  return 0;
}
