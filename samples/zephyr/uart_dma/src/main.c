/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "wirelink/wirelink.h"
#include "wirelink/zephyr/uart_dma.h"

#define LINK_UART_NODE DT_ALIAS(wirelink_uart)
#define MAX_PAYLOAD 256U
#define UNIT_STORAGE 320U
#define RX_RING_STORAGE 640U

static wl_ctx_t link;
static wl_zephyr_uart_dma_t uart_adapter;
static uint8_t tx_payload[MAX_PAYLOAD];
static uint8_t tx_unit[UNIT_STORAGE];
static uint8_t control_unit[64];
static uint8_t rx_fifo[RX_RING_STORAGE];
static uint8_t rx_fallback[UNIT_STORAGE];

int main(void) {
  const struct device *uart = DEVICE_DT_GET(LINK_UART_NODE);
  const wl_config_t config = {
      .max_payload_len = MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = UINT64_C(0x45535033325333),
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
  const wl_zephyr_uart_dma_config_t adapter_config = {
      .uart = uart,
      .link = &link,
      .maximum_chunk = 256U,
      .timeout_us = 1000,
      .tx_timeout_us = SYS_FOREVER_US,
      .wait_for_tx_idle = true,
  };
  int result;

  if (!device_is_ready(uart)) {
    printk("Wirelink UART is not ready\n");
    return 0;
  }
  result = wl_init(&link, &config, &storage);
  if (result != WL_OK) {
    printk("wl_init failed: %d\n", result);
    return 0;
  }
  result = wl_zephyr_uart_dma_init(&uart_adapter, &adapter_config);
  if (result == WL_OK) {
    result = wl_zephyr_uart_dma_start(&uart_adapter);
  }
  if (result != WL_OK) {
    printk("UART DMA adapter start failed: %d\n", result);
    return 0;
  }

  printk("Wirelink UART DMA endpoint ready\n");
  for (;;) {
    wl_event_t event;

    memset(&event, 0, sizeof(event));
    result = wl_poll(&link, k_uptime_get_32(), &event);
    if (result == WL_OK) {
      if (event.type == WL_EVT_UNRELIABLE_RX ||
          event.type == WL_EVT_RELIABLE_RX) {
        printk("RX message=%u bytes=%u reliable=%u\n", event.message_id,
               (unsigned int)event.payload_len,
               event.type == WL_EVT_RELIABLE_RX);
        /* Decode or consume the borrowed payload before releasing it. */
        wl_event_release(&link, &event);
      } else if (event.type == WL_EVT_TX_TIMEOUT ||
                 event.type == WL_EVT_TX_FAILED) {
        printk("TX event=%u result=%d\n", event.type, event.io_result);
      }
    } else if (result != WL_ERR_NO_DATA) {
      printk("wl_poll failed: %d\n", result);
    }

    result = wl_zephyr_uart_dma_service(&uart_adapter);
    if (result != WL_OK && result != WL_ERR_WOULD_BLOCK) {
      printk("UART DMA service failed: %d\n", result);
    }
    k_sleep(K_MSEC(1));
  }
  return 0;
}
