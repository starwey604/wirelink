/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <zephyr/ztest.h>
#include <wirelink/usb_bulk_defaults.h>

static wl_ctx_t link;
static uint8_t payload[128], tx[256], control[32], fallback[256], units[3][256];
static wl_config_t config;
static wl_usb_bulk_config_t usb;

static void *setup(void) {
  config = (wl_config_t){.max_payload_len = sizeof(payload), .session_id = 1U};
  usb = wl_usb_bulk_native_packet(sizeof(units[0]), ARRAY_SIZE(units));
  const wl_storage_t storage = {
      .tx_payload = payload, .tx_payload_size = sizeof(payload),
      .tx_unit = tx, .tx_unit_size = sizeof(tx),
      .control_unit = control, .control_unit_size = sizeof(control),
      .rx_fallback = fallback, .rx_fallback_size = sizeof(fallback)};
  zassert_ok(wl_usb_bulk_config_apply(&config, usb));
  zassert_ok(wl_init(&link, &config, &storage));
  return NULL;
}

ZTEST(wirelink_usb_config, test_preinit_selection_and_strict_bounds) {
  wl_config_t original = config;
  wl_usb_bulk_config_t value = wl_usb_bulk_native_packet(145U, 3U);
  zassert_equal(value.integrity, WL_INTEGRITY_NONE);
  zassert_ok(wl_usb_bulk_config_apply(&original, value));
  zassert_equal(original.session_id, config.session_id);
  for (int integrity = WL_INTEGRITY_NONE; integrity <= WL_INTEGRITY_CRC32C; ++integrity) {
    value.integrity = (wl_integrity_t)integrity;
    value.rx_capacity = wl_frame_raw_size(sizeof(payload), value.integrity);
    original = config;
    zassert_equal(wl_usb_bulk_config_apply(&original, value), WL_ERR_BUF_TOO_SMALL);
    zassert_mem_equal(&original, &config, sizeof(config));
    ++value.rx_capacity;
    zassert_ok(wl_usb_bulk_config_apply(&original, value));
  }
  value = wl_usb_bulk_native_packet(SIZE_MAX, 3U);
  zassert_equal(wl_usb_bulk_config_validate(value, 128U), WL_ERR_INVALID_ARG);
  value = wl_usb_bulk_native_packet(256U, 1U);
  zassert_equal(wl_usb_bulk_config_validate(value, 128U), WL_ERR_INVALID_ARG);
  value.rx_slots = WL_RX_UNIT_QUEUE_MAX_SLOTS + 1U;
  zassert_equal(wl_usb_bulk_config_validate(value, 128U), WL_ERR_INVALID_ARG);
  value = usb; value.rx_slots = 0U;
  zassert_equal(wl_usb_bulk_config_validate(value, 128U), WL_ERR_INVALID_ARG);
}

ZTEST(wirelink_usb_config, test_explicit_queue_and_readonly_binding) {
  /* Each test owns a fresh link; setup is explicit and cannot reset a live queue. */
  setup();
  wl_rx_unit_queue_config_t queue;
  zassert_equal(wl_usb_bulk_config_check_rx(&link, usb), WL_ERR_NOT_INITIALIZED);
  zassert_equal(wl_usb_bulk_rx_init(&link, usb, &units[0][0], sizeof(units)-1U), WL_ERR_INVALID_ARG);
  zassert_ok(wl_usb_bulk_rx_init(&link, usb, &units[0][0], sizeof(units)));
  zassert_ok(wl_rx_unit_queue_get_config(&link, &queue));
  zassert_equal(queue.unit_size, 256U); zassert_equal(queue.slot_count, 3U);
  zassert_equal(queue.storage, &units[0][0]);
  wl_rx_unit_claim_t claim;
  uint8_t byte = 42U;
  const wl_wire_packet_t packet = {.type = WL_PACKET_DATA, .integrity = WL_INTEGRITY_NONE,
      .message_id = 9U, .payload = &byte, .payload_len = 1U};
  for (int i = 0; i < 3; ++i) {
    size_t length;
    zassert_ok(wl_rx_unit_claim(&link, 256U, &claim));
    zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, claim.span.data, 256U, &length));
    zassert_ok(wl_rx_unit_commit(&link, &claim, length));
  }
  wl_event_t event;
  zassert_ok(wl_poll(&link, 0U, &event));
  const uint8_t *leased = event.payload;
  zassert_ok(wl_usb_bulk_config_check_rx(&link, usb));
  zassert_equal(wl_usb_bulk_rx_init(&link, usb, &units[0][0], sizeof(units)), WL_ERR_INVALID_STATE);
  zassert_equal(*leased, byte);
  zassert_equal(wl_rx_unit_claim(&link, 256U, &claim), WL_ERR_WOULD_BLOCK);
  wl_event_release(&link, &event);
  for (int i = 0; i < 2; ++i) {
    zassert_ok(wl_poll(&link, 0U, &event));
    zassert_equal(event.payload[0], byte); wl_event_release(&link, &event);
  }
  wl_usb_bulk_config_t wrong = usb; wrong.rx_slots = 2U;
  zassert_equal(wl_usb_bulk_config_check_rx(&link, wrong), WL_ERR_INVALID_ARG);
  wrong = usb; wrong.integrity = WL_INTEGRITY_CRC16;
  zassert_equal(wl_usb_bulk_config_check_rx(&link, wrong), WL_ERR_INVALID_ARG);
  wl_config_t actual;
  zassert_ok(wl_get_config(&link, &actual));
  zassert_equal(actual.envelope, config.envelope);
  zassert_equal(actual.integrity, config.integrity);
}

ZTEST(wirelink_usb_config, test_usb_rejects_stream_link_without_mutating_it) {
  static wl_ctx_t stream;
  static uint8_t fifo[512];
  wl_config_t stream_config = config;
  stream_config.envelope = WL_ENVELOPE_COBS_STREAM;
  const wl_storage_t storage = {
      .tx_payload = payload, .tx_payload_size = sizeof(payload),
      .tx_unit = tx, .tx_unit_size = sizeof(tx),
      .control_unit = control, .control_unit_size = sizeof(control),
      .rx_fifo = fifo, .rx_fifo_size = sizeof(fifo),
      .rx_fallback = fallback, .rx_fallback_size = sizeof(fallback)};
  zassert_ok(wl_init(&stream, &stream_config, &storage));
  zassert_equal(wl_usb_bulk_config_check_rx(&stream, usb), WL_ERR_INVALID_ARG);
  zassert_equal(wl_usb_bulk_rx_init(&stream, usb, &units[0][0], sizeof(units)), WL_ERR_INVALID_ARG);
  wl_config_t actual;
  zassert_ok(wl_get_config(&stream, &actual));
  zassert_equal(actual.envelope, WL_ENVELOPE_COBS_STREAM);
}

ZTEST_SUITE(wirelink_usb_config, NULL, setup, NULL, NULL, NULL);
