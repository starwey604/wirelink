/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>

#include "wirelink/frame.h"
#include "wirelink/wirelink.h"

enum { MAX_INPUT = 4096, MAX_PAYLOAD = 128 };

static wl_sink_result_t discard_sink(void *user_data, wl_io_token_t token,
                                     const uint8_t *data, size_t length) {
  (void)user_data;
  (void)token;
  (void)data;
  (void)length;
  return WL_SINK_SENT;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  wl_ctx_t context = {0};
  wl_config_t config = {
      .max_payload_len = MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = UINT64_C(0xF00D1234),
      .max_retries = 1U,
      .ack_timeout_ms = 10U,
      .max_transmission_unit = WL_FRAME_MAX_COBS_LEN,
  };
  uint8_t tx_payload[MAX_PAYLOAD];
  uint8_t tx_unit[WL_FRAME_MAX_COBS_LEN];
  uint8_t control_unit[WL_FRAME_MAX_COBS_LEN];
  uint8_t rx_fifo[WL_FRAME_MAX_COBS_LEN];
  uint8_t rx_fallback[WL_FRAME_MAX_COBS_LEN];
  wl_storage_t storage = {
      .tx_payload = tx_payload,
      .tx_payload_size = sizeof(tx_payload),
      .tx_unit = tx_unit,
      .tx_unit_size = sizeof(tx_unit),
      .control_unit = control_unit,
      .control_unit_size = sizeof(control_unit),
      .rx_fifo = rx_fifo,
      .rx_fifo_size = sizeof(rx_fifo),
      .rx_fallback = rx_fallback,
      .rx_fallback_size = sizeof(rx_fallback),
  };
  size_t offset = 0U;
  wl_time_ms_t now = 0U;

  if (size > MAX_INPUT || wl_init(&context, &config, &storage) != WL_OK ||
      wl_set_sink(&context, discard_sink, NULL) != WL_OK) {
    return 0;
  }
  while (offset < size) {
    size_t chunk = 1U + (data[offset] % 31U);
    size_t accepted = 0U;
    wl_event_t event = {0};
    if (chunk > size - offset) {
      chunk = size - offset;
    }
    (void)wl_feed_bytes(&context, data + offset, chunk, &accepted);
    offset += accepted;
    if (accepted == 0U) {
      (void)wl_feed_recover_reset(&context);
      ++offset;
    }
    if (wl_poll(&context, now++, &event) == WL_OK) {
      wl_event_release(&context, &event);
    }
  }
  for (unsigned int iteration = 0U; iteration < 8U; ++iteration) {
    wl_event_t event = {0};
    if (wl_poll(&context, now++, &event) != WL_OK) {
      break;
    }
    wl_event_release(&context, &event);
  }
  return 0;
}
