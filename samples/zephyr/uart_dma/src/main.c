/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "wirelink/kernel.h"
#include "wirelink/platform.h"
#include "wirelink/zephyr/kernel_uart_dma.h"
#include "wirelink/zephyr/kernel_timeout.h"

#define FRAME_MAX 260U /* Four-byte native prefix plus 256 opaque payload bytes. */
static wl_kernel_t endpoint;
static wl_packet_port_t port;
static wl_packet_stream_t stream;
static wl_byte_fifo_t fifo;
static wl_kernel_uart_dma_t adapter;
static uint8_t snapshots[WL_KERNEL_TX_COUNT][FRAME_MAX];
static uint8_t rx[2][FRAME_MAX + 1], tx[272], staging[640], dma[2][256];

/* This diagnostic tool accepts ordinary opaque DATA; it provides no RPC
 * business handler. The borrowed frame is consumed before returning. */
static wl_err_t receive(wl_kernel_t *core, const wl_kernel_frame_t *frame) {
  (void)core;
  printk("RX message=%u bytes=%u\n", frame->message_id, (unsigned)frame->length);
  return WL_OK;
}
int main(void) {
  const struct device *uart = DEVICE_DT_GET(DT_ALIAS(wirelink_uart));
  uint64_t instance;
  if (!device_is_ready(uart)) { printk("Wirelink UART unavailable\n"); return 1; }
  wl_environment_t environment = wl_platform_environment();
  int error = wl_session_next(environment.session, 0, &instance);
  if (error != WL_OK) { printk("Fresh instance unavailable: %d\n", error); return 1; }
  error = wl_byte_fifo_init(&fifo, (wl_span_t){staging, sizeof(staging)},
    wl_kernel_uart_dma_notify, &adapter);
  if (error != WL_OK) return 1;
  wl_packet_config_t packet = {.instance = instance, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx),
    .submit = wl_packet_stream_cobs_crc16_submit, .notify = wl_packet_stream_notify,
    .user = &stream};
  if (wl_packet_init(&port, &packet) != WL_OK) return 1;
  wl_packet_stream_config_t connector = {.port = &port, .tx_storage = {tx, sizeof(tx)},
    .maximum_frame_size = FRAME_MAX, .physical_submit = wl_kernel_uart_dma_submit,
    .notify = wl_kernel_uart_dma_notify, .user = &adapter};
  if (wl_packet_stream_cobs_crc16_init(&stream, &connector) != WL_OK) return 1;
  wl_kernel_config_t config = {.port = &port, .route = receive};
  for (unsigned i = 0; i < WL_KERNEL_TX_COUNT; ++i)
    config.tx_storage[i] = (wl_span_t){snapshots[i], sizeof(snapshots[i])};
  if (wl_kernel_init(&endpoint, &config) != WL_OK) return 1;
  wl_kernel_uart_dma_config_t physical = {.uart = uart, .port = &port, .fifo = &fifo,
    .stream = &stream, .rx_buffers = {{dma[0], sizeof(dma[0])}, {dma[1], sizeof(dma[1])}},
    .timeout_us = 1000, .tx_timeout_us = SYS_FOREVER_US, .wait_for_tx_idle = true};
  error = wl_kernel_uart_dma_init(&adapter, &physical);
  if (error != WL_OK) { printk("UART DMA init: %d\n", error); return 1; }
  error = wl_kernel_uart_dma_start(&adapter);
  if (error == WL_OK) printk("Wirelink UART DMA native DATA / COBS CRC16 ready\n");
  while (error == WL_OK) {
    const uint32_t queried_at = k_uptime_get_32();
    error = wl_kernel_uart_dma_service(&adapter, queried_at);
    if (error == WL_ERR_WOULD_BLOCK || error == WL_ERR_CORRUPT_PAYLOAD) {
      /* Gaps/malformed frames remain observable; parser resynchronizes. */
      if (error == WL_ERR_CORRUPT_PAYLOAD) printk("UART malformed frame / gap\n");
      error = WL_OK;
    }
    if (error != WL_OK) break;
    error = wl_kernel_step(&endpoint, queried_at, 16);
    if (error != WL_OK) break;
    int has_deadline = 0; uint32_t deadline = 0;
    int runnable = wl_kernel_ready(&endpoint, queried_at, &has_deadline, &deadline);
    wl_io_readiness_t io = wl_kernel_uart_dma_readiness(&adapter, queried_at);
    uint32_t maximum = io.next_deadline_ms;
    if (has_deadline && deadline - queried_at < maximum) maximum = deadline - queried_at;
    const uint32_t elapsed = k_uptime_get_32() - queried_at;
    if (runnable || io.runnable || (maximum != UINT32_MAX && elapsed >= maximum)) continue;
    error = wl_kernel_uart_dma_wait(&adapter,
      wl_kernel_timeout_ms(maximum == UINT32_MAX ? maximum : maximum - elapsed));
    if (error == WL_ERR_NO_DATA) error = WL_OK;
  }
  printk("UART owner stopping: %d\n", error);
  (void)wl_kernel_close(&endpoint);
  /* Joins real driver buffer returns and dispatched callbacks before reuse. */
  wl_kernel_uart_dma_quiesce(&adapter);
  (void)wl_kernel_step(&endpoint, k_uptime_get_32(), 16);
  if (!wl_kernel_quiescent(&endpoint)) { printk("UART stop incomplete\n"); return 1; }
  return 0;
}
