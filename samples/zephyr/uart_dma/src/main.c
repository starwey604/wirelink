/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "wirelink/zephyr/node.h"
#include "wirelink/platform.h"
#include "wirelink/zephyr/uart_dma.h"

#define FRAME_MAX 260U /* Four-byte native prefix plus 256 opaque payload bytes. */
static wl_node_t node;
static wl_packet_stream_t stream;
WL_ZEPHYR_UART_DMA_DEFINE(serial, DEVICE_DT_GET(DT_ALIAS(wirelink_uart)), 640U, 256U);
static uint8_t snapshots[WL_ENDPOINT_TX_COUNT][FRAME_MAX];
static uint8_t rx[2][273], tx[272];

/* This diagnostic tool accepts ordinary opaque DATA; it provides no RPC
 * business handler. The borrowed frame is consumed before returning. */
static wl_err_t receive(wl_endpoint_t *core, const wl_wire_frame_t *frame) {
  (void)core;
  printk("RX message=%u bytes=%u\n", frame->message_id, (unsigned)frame->length);
  return WL_OK;
}
int main(void) {
  uint32_t instance;
  if (!device_is_ready(serial.config.uart)) { printk("Wirelink UART unavailable\n"); return 1; }
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
  error = wl_node_bind_transport(&node, &layout,
      wl_zephyr_uart_dma_transport(&serial.adapter, &serial.config));
  if (error != WL_OK) { printk("UART DMA bind: %d\n", error); return 1; }
  printk("Wirelink UART DMA native DATA / COBS CRC16 ready\n");
  for (;;) {
    error = wl_zephyr_node_poll(&node, UINT32_MAX);
    if (error == WL_OK || error == WL_ERR_WOULD_BLOCK) continue;
    if (!wl_err_is_recoverable_frame(error)) break;
    printk("UART rejected frame: %d\n", error);
  }
  printk("UART owner stopping: %d\n", error);
  return wl_zephyr_node_close(&node, UINT32_MAX) == WL_OK ? 0 : 1;
}
