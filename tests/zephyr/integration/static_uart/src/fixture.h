/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_TEST_UART_FIXTURE_H_
#define WIRELINK_TEST_UART_FIXTURE_H_
#include "wirelink/node.h"

static inline wl_err_t bind_uart(wl_node_t *node, wl_packet_stream_t *stream, uint32_t instance,
                                 wl_span_t rx, wl_span_t wire, wl_transport_t *transport) {
  static const wl_node_stream_ops_t cobs = {wl_packet_stream_cobs_crc16_init,
    wl_packet_stream_cobs_crc16_submit, wl_packet_stream_cobs_crc16_feed,
    wl_packet_stream_abort_rx, wl_packet_stream_notify, WL_NODE_COBS_CRC16};
  static uint8_t snapshots[WL_ENDPOINT_TX_COUNT][64];
  uint16_t bound = (uint16_t)wl_packet_stream_cobs_crc16_bound(64U);
  wl_node_layout_t layout = {.instance = instance,
    .storage = wl_endpoint_storage_uniform(snapshots[0], sizeof(snapshots[0])), .rx_storage = rx.data,
    .rx_storage_size = rx.length, .rx_frame_max = 64U, .tx_frame_max = 64U,
    .rx_wire_max = bound, .tx_wire_max = bound, .has_tx = 1U, .stream = stream,
    .stream_tx_wire = wire, .stream_ops = &cobs};
  return wl_node_bind_transport(node, &layout, transport);
}
#endif
