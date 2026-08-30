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
#include "wirelink/zephyr/uart_irq.h"

#define MAX_PAYLOAD 512U
#define UNIT_STORAGE 576U
#define RX_RING_STORAGE 2048U

static wl_ctx_t link;
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
  const wl_config_t config = {
      .max_payload_len = MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .integrity = WL_INTEGRITY_CRC32C,
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
      .link = &link,
  };
  struct usbd_context *usb_context;
  uint8_t pending_payload[MAX_PAYLOAD];
  size_t pending_payload_len = 0U;
  uint16_t pending_message_id = 0U;
  bool echo_pending = false;
  int result;

  if (!device_is_ready(cdc)) {
    printk("CDC ACM device is not ready\n");
    return 0;
  }
  result = wl_init(&link, &config, &storage);
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

  result = wl_zephyr_uart_irq_init(&cdc_adapter, &adapter_config);
  if (result == WL_OK) {
    result = wl_zephyr_uart_irq_start(&cdc_adapter);
  }
  if (result != WL_OK) {
    printk("CDC IRQ adapter start failed: %d\n", result);
    return 0;
  }

  printk("Wirelink CDC IRQ endpoint ready (DMA=off)\n");
  for (;;) {
    if (!echo_pending) {
      wl_event_t pending_event;

      memset(&pending_event, 0, sizeof(pending_event));
      result = wl_poll(&link, k_uptime_get_32(), &pending_event);
      if (result == WL_OK &&
          (pending_event.type == WL_EVT_UNRELIABLE_RX ||
           pending_event.type == WL_EVT_RELIABLE_RX)) {
        pending_message_id = pending_event.message_id;
        pending_payload_len = pending_event.payload_len;
        memcpy(pending_payload, pending_event.payload, pending_payload_len);
        wl_event_release(&link, &pending_event);
        echo_pending = true;
      }
    }
    (void)wl_zephyr_uart_irq_service(&cdc_adapter);
    if (echo_pending) {
      result = wl_send_unreliable(&link, pending_message_id, pending_payload,
                                  pending_payload_len);
      if (result == WL_OK) {
        echo_pending = false;
      } else if (result != WL_ERR_BUSY) {
        echo_pending = false;
      }
    }
    k_yield();
  }
  return 0;
}
