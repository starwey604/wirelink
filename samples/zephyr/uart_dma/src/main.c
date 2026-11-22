/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "wirelink/node.h"
#include "wirelink/platform.h"
#include "wirelink/zephyr/uart_dma.h"
#include "wirelink/zephyr/detail/timeout.h"

#define FRAME_MAX 260U /* Four-byte native prefix plus 256 opaque payload bytes. */
static wl_node_t node;
static wl_packet_stream_t stream;
static wl_zephyr_uart_dma_t adapter;
static uint8_t snapshots[WL_ENDPOINT_TX_COUNT][FRAME_MAX];
static uint8_t rx[2][273], tx[272], staging[640], dma[2][256];

/* This diagnostic tool accepts ordinary opaque DATA; it provides no RPC
 * business handler. The borrowed frame is consumed before returning. */
static wl_err_t receive(wl_endpoint_t *core, const wl_wire_frame_t *frame) {
  (void)core;
  printk("RX message=%u bytes=%u\n", frame->message_id, (unsigned)frame->length);
  return WL_OK;
}
int main(void) {
  const struct device *uart = DEVICE_DT_GET(DT_ALIAS(wirelink_uart));
  uint32_t instance;
  if (!device_is_ready(uart)) { printk("Wirelink UART unavailable\n"); return 1; }
  wl_environment_t environment = wl_platform_environment();
  int error = wl_session_next(environment.session, 0, &instance);
  if (error != WL_OK) { printk("Fresh instance unavailable: %d\n", error); return 1; }
  static const wl_node_stream_ops_t cobs = {
    wl_packet_stream_cobs_crc16_init, wl_packet_stream_cobs_crc16_submit,
    wl_packet_stream_cobs_crc16_feed, wl_packet_stream_abort_rx, wl_packet_stream_notify,
    WL_NODE_COBS_CRC16};
  size_t bound = wl_packet_stream_cobs_crc16_bound(FRAME_MAX);
  const wl_node_layout_t layout = {.instance = instance,
    .storage = wl_endpoint_storage_uniform(snapshots[0], FRAME_MAX), .route = receive,
    .rx_frame_max = FRAME_MAX, .tx_frame_max = FRAME_MAX, .rx_wire_max = bound, .tx_wire_max = bound,
    .rx_storage = rx[0], .rx_storage_size = sizeof(rx), .stream = &stream,
    .stream_ops = &cobs, .stream_tx_wire = {tx, sizeof(tx)}, .has_tx = 1};
  wl_zephyr_uart_dma_config_t physical = {.uart = uart, .fifo_storage = {staging, sizeof(staging)},
    .rx_buffers = {{dma[0], sizeof(dma[0])}, {dma[1], sizeof(dma[1])}},
    .timeout_us = 1000, .tx_timeout_us = SYS_FOREVER_US, .wait_for_tx_idle = true};
  error = wl_node_bind_transport(&node, &layout, wl_zephyr_uart_dma_transport(&adapter, &physical));
  if (error != WL_OK) { printk("UART DMA bind: %d\n", error); return 1; }
  if (error == WL_OK) printk("Wirelink UART DMA native DATA / COBS CRC16 ready\n");
  while (error == WL_OK) {
    const uint32_t queried_at = k_uptime_get_32();
    error = wl_node_step(&node, queried_at);
    if (error == WL_ERR_WOULD_BLOCK || error == WL_ERR_CORRUPT_PAYLOAD) {
      /* Gaps/malformed frames remain observable; parser resynchronizes. */
      if (error == WL_ERR_CORRUPT_PAYLOAD) printk("UART malformed frame / gap\n");
      error = WL_OK;
    }
    if (error == WL_OK) error = wl_node_wait(&node, k_uptime_get_32(), UINT32_MAX);
  }
  printk("UART owner stopping: %d\n", error);
  do {
    error = wl_node_close(&node, k_uptime_get_32());
    if (error == WL_ERR_WOULD_BLOCK || error == WL_ERR_BUSY) k_sleep(K_TICKS(1));
  } while (error == WL_ERR_WOULD_BLOCK || error == WL_ERR_BUSY);
  return error == WL_OK ? 0 : 1;
}
