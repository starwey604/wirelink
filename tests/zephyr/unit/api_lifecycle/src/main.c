/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/wirelink.h"

static void init_native(wl_ctx_t *ctx) {
  static uint8_t tx_payload[32];
  static uint8_t tx_unit[64];
  static uint8_t control_unit[32];
  static uint8_t rx_fallback[64];
  const wl_config_t config = {
      .max_payload_len = sizeof(tx_payload),
      .envelope = WL_ENVELOPE_NATIVE_PACKET,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = UINT64_C(0x4150494C49464531),
      .max_retries = 1U,
      .ack_timeout_ms = 10U,
      .max_transmission_unit = sizeof(tx_unit),
  };
  const wl_storage_t storage = {
      .tx_payload = tx_payload,
      .tx_payload_size = sizeof(tx_payload),
      .tx_unit = tx_unit,
      .tx_unit_size = sizeof(tx_unit),
      .control_unit = control_unit,
      .control_unit_size = sizeof(control_unit),
      .rx_fallback = rx_fallback,
      .rx_fallback_size = sizeof(rx_fallback),
  };

  memset(ctx, 0, sizeof(*ctx));
  zassert_ok(wl_init(ctx, &config, &storage));
}

ZTEST(wirelink_api_lifecycle, test_context_apis_reject_before_init) {
  wl_ctx_t ctx = {0};
  wl_config_t config = {0};
  wl_rx_counters_t counters = {0};
  wl_tx_state_t state = WL_TX_STATE_IDLE;
  wl_tx_result_t tx_result = {0};
  wl_tx_handle_t handle = 0U;
  wl_span_t span = {0};
  wl_rx_dma_claim_t claim = {0};
  wl_event_t event = {0};
  size_t accepted = 0U;

  zassert_equal(wl_set_sink(&ctx, NULL, NULL), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_get_config(&ctx, &config), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_rx_get_counters(&ctx, &counters), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_send_unreliable(&ctx, 1U, NULL, 0U),
                WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_send_reliable(&ctx, 1U, NULL, 0U, &handle),
                WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_tx_status(&ctx, 1U, &state), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_tx_cancel(&ctx, 1U), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_tx_take(&ctx, 1U, &tx_result), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_tx_complete(&ctx, 1U, WL_OK), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_feed_unit(&ctx, NULL, 0U), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_feed_bytes(&ctx, NULL, 0U, &accepted),
                WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_rx_reserve(&ctx, &span), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_rx_commit(&ctx, 0U), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_rx_dma_claim(&ctx, 1U, &claim), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_rx_dma_publish(&ctx, &claim, 0U, 0U),
                WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_rx_dma_finish(&ctx, &claim), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_rx_dma_abort(&ctx), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_feed_recover_reset(&ctx), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_poll(&ctx, 0U, &event), WL_ERR_NOT_INITIALIZED);

  wl_rx_note_overflow(&ctx);
  wl_event_release(&ctx, &event);
}

ZTEST(wirelink_api_lifecycle, test_profile_domains_and_timeout_bounds) {
  wl_config_t config = {
      .max_payload_len = 32U,
      .envelope = WL_ENVELOPE_NATIVE_PACKET,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = UINT64_C(1),
  };
  wl_storage_requirements_t requirements = {0};

  config.envelope = -1;
  zassert_equal(wl_config_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config.envelope = WL_ENVELOPE_BUS_LENGTH16 + 1;
  zassert_equal(wl_config_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config.envelope = WL_ENVELOPE_NATIVE_PACKET;
  config.integrity = -1;
  zassert_equal(wl_config_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config.integrity = WL_INTEGRITY_CRC32C + 1;
  zassert_equal(wl_config_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config.integrity = WL_INTEGRITY_CRC32C;
  config.ack_timeout_ms = 0U;
  zassert_ok(wl_config_requirements(&config, &requirements));
  config.ack_timeout_ms = UINT32_C(0x7FFFFFFF);
  zassert_ok(wl_config_requirements(&config, &requirements));
  config.ack_timeout_ms = UINT32_C(0x80000000);
  zassert_equal(wl_config_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
  config.ack_timeout_ms = UINT32_MAX;
  zassert_equal(wl_config_requirements(&config, &requirements),
                WL_ERR_INVALID_ARG);
}

ZTEST(wirelink_api_lifecycle, test_poll_clears_output_when_idle) {
  wl_ctx_t ctx;
  wl_event_t event;

  init_native(&ctx);
  memset(&event, 0xA5, sizeof(event));
  zassert_equal(wl_poll(&ctx, 7U, &event), WL_ERR_NO_DATA);
  zassert_equal(event.type, WL_EVT_NONE);
  zassert_is_null(event.payload);
  zassert_equal(event.payload_len, 0U);
  zassert_equal(event.handle, 0U);
  zassert_equal(event.lease, 0U);
  zassert_equal(wl_feed_recover_reset(&ctx), WL_ERR_NOT_SUPPORTED);
}

ZTEST_SUITE(wirelink_api_lifecycle, NULL, NULL, NULL, NULL, NULL);
