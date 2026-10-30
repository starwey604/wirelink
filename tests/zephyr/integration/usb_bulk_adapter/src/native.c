/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise the real class callbacks and queue with a controlled UDC boundary. */
#include "fixture.h"
#include "wirelink/frame.h"

static struct net_buf *native_complete(struct usbd_class_data *class_data,
                                       wl_zephyr_usb_bulk_t *adapter,
                                       struct net_buf *buffer, size_t length,
                                       int error) {
  net_buf_add(buffer, length);
  zassert_ok(class_data->api->request(class_data, buffer, error));
  int result = wl_zephyr_usb_bulk_service(adapter);
  zassert_true(result == WL_OK || result == WL_ERR_WOULD_BLOCK || result == WL_ERR_IO);
  return take_queued(0x01U);
}

ZTEST(wirelink_usb_native, test_native_completion_contract) {
  static wl_ctx_t link;
  static wl_zephyr_usb_bulk_t adapter;
  static uint8_t tx_payload[1032], tx_unit[1052], control_unit[20];
  static uint8_t fallback[1052], units[2][2048];
  static struct usbd_context context;
  wl_config_t config = {.max_payload_len = sizeof(tx_payload),
      .envelope = WL_ENVELOPE_NATIVE_PACKET, .integrity = WL_INTEGRITY_NONE,
      .session_id = 1U, .max_transmission_unit = sizeof(tx_unit)};
  wl_storage_t storage = {.tx_payload = tx_payload, .tx_payload_size = sizeof(tx_payload),
      .tx_unit = tx_unit, .tx_unit_size = sizeof(tx_unit),
      .control_unit = control_unit, .control_unit_size = sizeof(control_unit),
      .rx_fallback = fallback, .rx_fallback_size = sizeof(fallback)};
  const wl_usb_bulk_config_t usb_config = wl_usb_bulk_native_packet(2048, 2);
  wl_zephyr_usb_bulk_config_t adapter_config = {.link = &link, .transport = usb_config};
  struct usbd_class_data *class_data = bulk_class();
  wl_zephyr_usb_bulk_stats_t before, after;
  wl_event_t event;
  uint8_t payload[60];
  size_t length;
  memset(payload, 0x5a, sizeof(payload));
  wl_wire_packet_t packet = {.type = WL_PACKET_DATA, .integrity = WL_INTEGRITY_NONE,
      .message_id = 103U, .payload = payload, .payload_len = sizeof(payload)};

  class_data->uds_ctx = &context;
  context.status.speed = USBD_SPEED_FS;
  configure_descriptor(bulk_descriptor(class_data, USBD_SPEED_FS, false), 0x01, 64);
  configure_descriptor(bulk_descriptor(class_data, USBD_SPEED_FS, true), 0x81, 64);
  zassert_ok(wl_usb_bulk_config_apply(&config, usb_config));
  zassert_ok(wl_init(&link, &config, &storage));
  zassert_equal(wl_zephyr_usb_bulk_init(&adapter, &adapter_config), WL_ERR_NOT_INITIALIZED);
  zassert_ok(wl_usb_bulk_rx_init(&link, usb_config, &units[0][0], sizeof(units)));
  adapter_config.transport.rx_capacity = 1048U;
  zassert_equal(wl_zephyr_usb_bulk_init(&adapter, &adapter_config), WL_ERR_BUF_TOO_SMALL);
  adapter_config.transport = usb_config;
  zassert_ok(wl_zephyr_usb_bulk_init(&adapter, &adapter_config));
  complete_out_inline = true;
  class_data->api->enable(class_data);
  zassert_true(wl_zephyr_usb_bulk_readiness(&adapter).runnable);
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));
  struct net_buf *buffer = take_queued(0x01);
  zassert_not_null(buffer);
  zassert_equal(net_buf_tailroom(buffer), 2048U);

  /* Two committed slots stay borrowed through the event lease. */
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET,
      buffer->data, net_buf_tailroom(buffer), &length));
  zassert_equal(length, 64U);
  buffer = native_complete(class_data, &adapter, buffer, length, 0);
  zassert_not_null(buffer);
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET,
      buffer->data, net_buf_tailroom(buffer), &length));
  buffer = native_complete(class_data, &adapter, buffer, length, 0);
  zassert_is_null(buffer, "queue full must pause the producer");
  zassert_ok(wl_poll(&link, 0, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  zassert_equal(wl_zephyr_usb_bulk_service(&adapter), WL_ERR_WOULD_BLOCK);
  zassert_is_null(take_queued(0x01), "leased slot must not rearm");
  wl_event_release(&link, &event);
  zassert_ok(wl_zephyr_usb_bulk_service(&adapter));
  buffer = take_queued(0x01);
  zassert_not_null(buffer);
  zassert_ok(wl_poll(&link, 0, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  wl_event_release(&link, &event);

  /* A successful empty transfer returns the claim without a frame or error. */
  wl_rx_unit_claim_t old_claim = adapter.rx_unit_claim;
  wl_zephyr_usb_bulk_get_stats(&adapter, &before);
  buffer = native_complete(class_data, &adapter, buffer, 0, 0);
  wl_zephyr_usb_bulk_get_stats(&adapter, &after);
  zexpect_equal(after.errors, before.errors, "empty success is not an IO error");
  zassert_not_null(buffer, "empty success must rearm");
  zexpect_equal(wl_rx_unit_abort(&link, &old_claim), WL_ERR_NOT_FOUND);
  zexpect_equal(wl_rx_unit_commit(&link, &old_claim, 64), WL_ERR_NOT_FOUND);
  zexpect_equal(wl_poll(&link, 0, &event), WL_ERR_NO_DATA);

  /* Re-enable may precede the old request's cancellation callback. */
  wl_zephyr_usb_bulk_get_stats(&adapter, &before);
  class_data->api->disable(class_data);
  class_data->api->enable(class_data);
  buffer = native_complete(class_data, &adapter, buffer, 0, -ECONNABORTED);
  wl_zephyr_usb_bulk_get_stats(&adapter, &after);
  zexpect_equal(after.errors, before.errors, "old cancellation is not a new boundary fault");
  zassert_not_null(buffer, "late cancellation must rearm the new configuration");

  /* A full request cannot establish that the terminating short packet/ZLP
   * has been consumed. It must never be committed as a complete unit. */
  memset(buffer->data, 0, net_buf_tailroom(buffer));
  wl_zephyr_usb_bulk_get_stats(&adapter, &before);
  buffer = native_complete(class_data, &adapter, buffer,
      TEST_USB_NATIVE_ERROR ? 12 : 2048, TEST_USB_NATIVE_ERROR ? -ETIMEDOUT : 0);
  wl_zephyr_usb_bulk_get_stats(&adapter, &after);
  zexpect_equal(after.rx_bytes, before.rx_bytes, "unterminated unit was committed");
  zexpect_is_null(buffer, "lost boundary must stay stopped until transport reset");
  zexpect_true(after.errors > before.errors);
  const wl_usb_bulk_rx_fault_t fault = TEST_USB_NATIVE_ERROR
      ? WL_USB_BULK_RX_FAULT_TRANSFER : WL_USB_BULK_RX_FAULT_BOUNDARY_LOST;
  zassert_equal(wl_zephyr_usb_bulk_rx_fault(&adapter), fault);
  zassert_equal(wl_zephyr_usb_bulk_service(&adapter), WL_ERR_IO);
  zassert_false(wl_zephyr_usb_bulk_readiness(&adapter).runnable);
  wl_zephyr_usb_bulk_reset_stats(&adapter);
  wl_zephyr_usb_bulk_get_stats(&adapter, &after);
  zassert_equal(after.errors, 0U);
  zassert_equal(after.rx_fault, fault, "telemetry reset cannot erase recovery state");
  class_data->api->enable(class_data);
  zassert_equal(wl_zephyr_usb_bulk_rx_fault(&adapter), fault);
  zassert_is_null(take_queued(0x01), "enable without disable is not recovery");
  if (buffer != NULL) {
    /* Keep the independent timeout failure even if the full-size check failed. */
    wl_zephyr_usb_bulk_get_stats(&adapter, &before);
    buffer = native_complete(class_data, &adapter, buffer, 12, -ETIMEDOUT);
    wl_zephyr_usb_bulk_get_stats(&adapter, &after);
    zexpect_equal(after.rx_bytes, before.rx_bytes);
    zexpect_is_null(buffer, "timeout suffix must not be accepted as a new frame");
    zexpect_true(after.errors > before.errors);
  }
  /* Public recovery requests a real context disable/enable. Wrappers model
   * only lifecycle calls; this is not a physical disconnect/controller test. */
  disable_result = -EIO;
  zassert_equal(wl_zephyr_usb_bulk_recover(&adapter), WL_ERR_IO);
  zassert_equal(wl_zephyr_usb_bulk_rx_fault(&adapter), fault);
  disable_result = 0; enable_result = -EIO;
  zassert_equal(wl_zephyr_usb_bulk_recover(&adapter), WL_ERR_IO);
  unsigned disabled = disable_calls;
  enable_result = 0;
  zassert_ok(wl_zephyr_usb_bulk_recover(&adapter));
  zassert_equal(disable_calls, disabled, "retry enable after disable succeeded");
  zassert_equal(wl_zephyr_usb_bulk_recover(&adapter), WL_ERR_WOULD_BLOCK);
  zassert_equal(wl_zephyr_usb_bulk_rx_fault(&adapter), fault,
                "wait for actual host configuration");
  class_data->api->enable(class_data);
  zassert_equal(wl_zephyr_usb_bulk_rx_fault(&adapter), WL_USB_BULK_RX_FAULT_NONE);
  buffer = take_queued(0x01);
  zassert_not_null(buffer);
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET,
      buffer->data, net_buf_tailroom(buffer), &length));
  buffer = native_complete(class_data, &adapter, buffer, length, 0);
  zassert_not_null(buffer);
  zassert_ok(wl_poll(&link, 0, &event));
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&link, &event);
  /* A fast bus reconfiguration must not erase an unobserved fault report. */
  net_buf_add(buffer, net_buf_tailroom(buffer));
  zassert_ok(class_data->api->request(class_data, buffer, 0));
  zassert_true(wl_zephyr_usb_bulk_readiness(&adapter).runnable);
  class_data->api->disable(class_data);
  class_data->api->enable(class_data);
  zassert_equal(wl_zephyr_usb_bulk_rx_fault(&adapter), WL_USB_BULK_RX_FAULT_NONE);
  zassert_true(wl_zephyr_usb_bulk_readiness(&adapter).runnable);
  zassert_equal(wl_zephyr_usb_bulk_service(&adapter), WL_ERR_IO);
  wl_zephyr_usb_bulk_get_stats(&adapter, &after);
  zassert_equal(after.last_rx_fault, WL_USB_BULK_RX_FAULT_BOUNDARY_LOST);
  zassert_false(wl_zephyr_usb_bulk_readiness(&adapter).runnable);
  buffer = take_queued(0x01);
  zassert_not_null(buffer);
  class_data->api->disable(class_data);
  (void)native_complete(class_data, &adapter, buffer, 0, -ECONNABORTED);
}

ZTEST_SUITE(wirelink_usb_native, NULL, NULL, NULL, NULL, NULL);
