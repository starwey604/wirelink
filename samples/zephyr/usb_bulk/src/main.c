/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usbd.h>

#include "wirelink/wirelink.h"
#include "wirelink/zephyr/usb_bulk.h"

#define MAX_PAYLOAD 512U
#define UNIT_STORAGE 576U
#define RX_RING_STORAGE 2048U

static wl_ctx_t link;
static wl_zephyr_usb_bulk_t usb_adapter;
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
  const wl_config_t config = {
      .max_payload_len = MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = UINT64_C(0x55534242554C4B31),
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
  const wl_zephyr_usb_bulk_config_t adapter_config = {
      .link = &link,
      .maximum_rx_size = UNIT_STORAGE,
  };
  struct usbd_context *usb_context;
  uint8_t pending_payload[MAX_PAYLOAD];
  size_t pending_payload_len = 0U;
  uint16_t pending_message_id = 0U;
  bool echo_pending = false;
  int result;

  result = wl_init(&link, &config, &storage);
  if (result != WL_OK) {
    printk("wl_init failed: %d\n", result);
    return 0;
  }
  result = wl_zephyr_usb_bulk_init(&usb_adapter, &adapter_config);
  if (result != WL_OK) {
    printk("USB bulk adapter init failed: %d\n", result);
    return 0;
  }

  usb_context = sample_usbd_init_device(usb_message);
  if (usb_context == NULL) {
    printk("USB device init failed\n");
    return 0;
  }
  if (!usbd_can_detect_vbus(usb_context)) {
    result = usbd_enable(usb_context);
    if (result != 0) {
      printk("USB enable failed: %d\n", result);
      return 0;
    }
  }

  printk("Wirelink vendor bulk endpoint ready (OUT=0x01 IN=0x81 DMA=off)\n");
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

    (void)wl_zephyr_usb_bulk_service(&usb_adapter);
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
