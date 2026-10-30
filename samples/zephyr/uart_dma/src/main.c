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
#define RX_DMA_BUFFER_SIZE 256U

static wl_endpoint_t endpoint;
static wl_time_ms_t now(void *context) { (void)context; return k_uptime_get_32(); }
static wl_pump_event_disposition_t receive(void *context, wl_ctx_t *link,
    const wl_event_t *event, wl_time_ms_t time) {
  (void)context; (void)time;
  if (event->type == WL_EVT_UNRELIABLE_RX || event->type == WL_EVT_RELIABLE_RX) {
    printk("RX message=%u bytes=%u reliable=%u\n", event->message_id,
        (unsigned)event->payload_len, event->type == WL_EVT_RELIABLE_RX);
    wl_event_release(link, event);
    return WL_PUMP_EVENT_CONSUMED;
  }
  return WL_PUMP_EVENT_UNHANDLED;
}
static wl_zephyr_uart_dma_t uart_adapter;
static uint8_t tx_payload[MAX_PAYLOAD];
static uint8_t tx_unit[UNIT_STORAGE];
static uint8_t control_unit[64];
static uint8_t rx_fifo[RX_RING_STORAGE];
static uint8_t rx_fallback[UNIT_STORAGE];
static uint8_t rx_dma_buffers[2][RX_DMA_BUFFER_SIZE];

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
      .rx_buffers = {
          {rx_dma_buffers[0], sizeof(rx_dma_buffers[0])},
          {rx_dma_buffers[1], sizeof(rx_dma_buffers[1])},
      },
      .timeout_us = 1000,
      .tx_timeout_us = SYS_FOREVER_US,
      .wait_for_tx_idle = true,
  };
  int result;

  if (!device_is_ready(uart)) {
    printk("Wirelink UART is not ready\n");
    return 0;
  }
  const wl_clock_t clock = {now, NULL};
  const wl_pump_hooks_t application = {.on_event = receive};
  result = wl_endpoint_init(&endpoint, &config, &storage, &clock, &application);
  if (result != WL_OK) {
    printk("wl_init failed: %d\n", result);
    return 0;
  }
  result = wl_zephyr_uart_dma_attach(&uart_adapter, &endpoint, &adapter_config);
  if (result == WL_OK) {
    result = wl_zephyr_uart_dma_start(&uart_adapter);
  }
  if (result != WL_OK) {
    printk("UART DMA adapter start failed: %d\n", result);
    return 0;
  }

  printk("Wirelink UART DMA endpoint ready\n");
  for (;;) {
    result = wl_endpoint_step(&endpoint, 8U);
    if (result != WL_OK) break;
    wl_readiness_t readiness;
    wl_time_ms_t queried_at, wait_now;
    if (wl_endpoint_now(&endpoint, &queried_at) != WL_OK) break;
    if (wl_endpoint_get_readiness_at(&endpoint, queried_at, &readiness) != WL_OK) break;
    if (!readiness.runnable && readiness.next_deadline_ms != 0U) {
      const wl_waiter_t *waiter = wl_endpoint_waiter(&endpoint);
      if (wl_endpoint_now(&endpoint, &wait_now) != WL_OK) break;
      const uint32_t remaining = wl_wait_remaining(readiness.next_deadline_ms, queried_at, wait_now);
      if (remaining == 0U) continue;
      result = waiter->wait(waiter->user_data, remaining);
      if (result != WL_OK && result != WL_ERR_NO_DATA) break;
    }
  }
  wl_endpoint_close(&endpoint);
  return 0;
}
