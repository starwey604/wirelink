/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <sample_usbd.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usbd.h>

#include "wirelink/wirelink.h"
#include "wirelink/usb_bulk_defaults.h"
#include "wirelink/zephyr/uart_irq.h"
#include "owner_wait.h"

#define MAX_PAYLOAD 512U
#define UNIT_STORAGE 576U
#define RX_RING_STORAGE 2048U

static wl_endpoint_t endpoint;
static uint8_t pending_payload[MAX_PAYLOAD];
static size_t pending_payload_len;
static uint16_t pending_message_id;
static bool echo_pending;
static wl_event_t held_rx;
static wl_time_ms_t now(void *context) { (void)context; return k_uptime_get_32(); }
static wl_pump_event_disposition_t receive(void *context, wl_ctx_t *link,
    const wl_event_t *event, wl_time_ms_t time) {
  (void)context; (void)time;
  if (event->type != WL_EVT_UNRELIABLE_RX && event->type != WL_EVT_RELIABLE_RX)
    return WL_PUMP_EVENT_UNHANDLED;
  if (!echo_pending) {
    pending_message_id = event->message_id;
    pending_payload_len = event->payload_len;
    memcpy(pending_payload, event->payload, pending_payload_len);
    echo_pending = true;
  }
  (void)link;
  held_rx = *event;
  return WL_PUMP_EVENT_LEASED;
}
static wl_readiness_t echo_readiness(const void *context, const wl_ctx_t *link, wl_time_ms_t time) {
  (void)context; (void)time;
  wl_readiness_t readiness = wl_readiness_idle();
  if (echo_pending) {
    readiness.runnable = wl_tx_can_submit(link, WL_DELIVERY_UNRELIABLE);
    readiness.blocked = !readiness.runnable;
  }
  return readiness;
}
static uint8_t echo_step(void *context, wl_ctx_t *link, wl_time_ms_t time) {
  (void)context; (void)time;
  int result = wl_send_unreliable(link, pending_message_id, pending_payload, pending_payload_len);
  if (result != WL_ERR_BUSY) {
    echo_pending = false;
    wl_event_release(link, &held_rx);
    memset(&held_rx, 0, sizeof(held_rx));
  }
  return result == WL_OK;
}
static wl_zephyr_uart_irq_t cdc_adapter;
static uint8_t tx_payload[MAX_PAYLOAD];
static uint8_t tx_unit[UNIT_STORAGE];
static uint8_t control_unit[64];
static uint8_t rx_fifo[RX_RING_STORAGE];
static uint8_t rx_fallback[UNIT_STORAGE];

static void usb_message(struct usbd_context *context,
                        const struct usbd_msg *message) {
  if (usbd_can_detect_vbus(context) && message->type == USBD_MSG_VBUS_READY) {
    (void)usbd_enable(context);
  }
  if (usbd_can_detect_vbus(context) &&
      message->type == USBD_MSG_VBUS_REMOVED) {
    (void)usbd_disable(context);
  }
}

int main(void) {
  const struct device *cdc = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
  wl_config_t config = {
      .max_payload_len = MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .session_id = UINT64_C(0x5553424344434931),
      .max_retries = 2U,
      .ack_timeout_ms = 20U,
      .max_transmission_unit = sizeof(tx_unit),
  };
  const wl_storage_t storage = {
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
  const wl_zephyr_uart_irq_config_t adapter_config = {
      .uart = cdc,
  };
  struct usbd_context *usb_context;
  int result;

  if (!device_is_ready(cdc)) {
    printk("CDC ACM device is not ready\n");
    return 0;
  }
  const wl_clock_t clock = {now, NULL};
  const wl_pump_hooks_t application = {.on_event = receive, .application_step = echo_step,
    .application_readiness = echo_readiness};
  (void)wl_config_apply_link_defaults(&config, wl_usb_bulk_link_defaults());
  result = wl_endpoint_init(&endpoint, &config, &storage, &clock, &application);
  if (result != WL_OK) {
    printk("wl_init failed: %d\n", result);
    return 0;
  }

  usb_context = sample_usbd_init_device(usb_message);
  if (usb_context == NULL) {
    printk("USB device init failed\n");
    return 0;
  }
  if (!usbd_can_detect_vbus(usb_context) && usbd_enable(usb_context) != 0) {
    printk("USB enable failed\n");
    return 0;
  }

  result = wl_zephyr_uart_irq_attach(&cdc_adapter, &endpoint, &adapter_config);
  if (result == WL_OK) {
    result = wl_zephyr_uart_irq_start(&cdc_adapter);
  }
  if (result != WL_OK) {
    printk("CDC IRQ adapter start failed: %d\n", result);
    return 0;
  }

  printk("Wirelink CDC IRQ endpoint ready (DMA=off)\n");
  for (;;) {
    result = wl_endpoint_step(&endpoint, 8U);
    if (result != WL_OK) break;
    result = cdc_wait_for_activity(&endpoint);
    if (result != WL_OK && result != WL_ERR_NO_DATA) break;
  }
  if (echo_pending) wl_event_release(wl_endpoint_link(&endpoint), &held_rx);
  wl_endpoint_close(&endpoint);
  return 0;
}
