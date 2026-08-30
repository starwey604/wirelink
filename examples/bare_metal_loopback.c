/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wirelink/wirelink.h"

#define EXAMPLE_MAX_PAYLOAD 64U
#define EXAMPLE_STORAGE_SIZE 128U

typedef struct {
  wl_ctx_t link;
  uint8_t tx_payload[EXAMPLE_MAX_PAYLOAD];
  uint8_t tx_unit[EXAMPLE_STORAGE_SIZE];
  uint8_t control_unit[EXAMPLE_STORAGE_SIZE];
  uint8_t rx_fallback[EXAMPLE_STORAGE_SIZE];
  uint8_t outbound[EXAMPLE_STORAGE_SIZE];
  size_t outbound_length;
} endpoint_t;

static wl_sink_result_t capture_unit(void *user_data, wl_io_token_t token,
                                     const uint8_t *data, size_t length) {
  endpoint_t *endpoint = user_data;

  (void)token;
  if (endpoint->outbound_length != 0U) {
    return WL_SINK_BUSY;
  }
  if (length > sizeof(endpoint->outbound)) {
    return WL_SINK_FAILED;
  }
  memcpy(endpoint->outbound, data, length);
  endpoint->outbound_length = length;
  return WL_SINK_SENT;
}

static int endpoint_init(endpoint_t *endpoint, uint64_t session_id) {
  const wl_config_t config = {
      .max_payload_len = EXAMPLE_MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_NATIVE_PACKET,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = session_id,
      .max_retries = 2U,
      .ack_timeout_ms = 20U,
      .max_transmission_unit = sizeof(endpoint->tx_unit),
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
  int result;

  memset(endpoint, 0, sizeof(*endpoint));
  result = wl_init(&endpoint->link, &config, &storage);
  if (result != WL_OK) {
    return result;
  }
  return wl_set_sink(&endpoint->link, capture_unit, endpoint);
}

/* A packet transport calls wl_feed_unit() once for each received datagram. */
static int deliver(endpoint_t *source, endpoint_t *destination) {
  int result;

  if (source->outbound_length == 0U) {
    return WL_ERR_NO_DATA;
  }
  result = wl_feed_unit(&destination->link, source->outbound,
                        source->outbound_length);
  source->outbound_length = 0U;
  return result;
}

int main(void) {
  endpoint_t controller;
  endpoint_t actuator;
  const uint8_t command[] = {0x01U, 0x02U, 0x00U, 0x04U};
  wl_tx_handle_t handle;
  wl_tx_result_t tx_result;
  wl_event_t event;

  if (endpoint_init(&controller, UINT64_C(0x1001)) != WL_OK ||
      endpoint_init(&actuator, UINT64_C(0x2002)) != WL_OK) {
    return 1;
  }

  if (wl_send_reliable(&controller.link, 0x42U, command, sizeof(command),
                       &handle) != WL_OK ||
      deliver(&controller, &actuator) != WL_OK) {
    return 2;
  }

  memset(&event, 0, sizeof(event));
  if (wl_poll(&actuator.link, 1U, &event) != WL_OK ||
      event.type != WL_EVT_RELIABLE_RX || event.message_id != 0x42U ||
      event.payload_len != sizeof(command) ||
      memcmp(event.payload, command, sizeof(command)) != 0) {
    return 3;
  }
  /* The borrowed payload remains valid until this explicit release. */
  wl_event_release(&actuator.link, &event);

  if (deliver(&actuator, &controller) != WL_OK) {
    return 4;
  }
  memset(&event, 0, sizeof(event));
  if (wl_poll(&controller.link, 2U, &event) != WL_OK ||
      event.type != WL_EVT_TX_SUCCESS || event.handle != handle) {
    return 5;
  }
  if (wl_tx_take(&controller.link, handle, &tx_result) != WL_OK ||
      tx_result.state != WL_TX_STATE_SUCCESS) {
    return 6;
  }

  puts("reliable native-packet round trip succeeded");
  return 0;
}
