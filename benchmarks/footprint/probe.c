/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include "demo.h"
#include "wirelink/byte_fifo.h"
#include "wirelink/packet_loopback.h"
#include "wirelink/packet_stream.h"
#define PREFIX "wirelink_footprint_v2"
static void type(const char *name, size_t size) {
  printf(PREFIX ",type,%s,%zu\n", name, size);
}
static size_t aligned(size_t size, size_t alignment) {
  return size + (alignment - size % alignment) % alignment;
}

static void assembly(const char *name, size_t rx_wire, size_t stream_tx, size_t byte_storage) {
  const size_t port = sizeof(wl_packet_port_t);
  const size_t rx = WL_PACKET_RX_SLOTS * (rx_wire + 1U);
  const size_t stream = stream_tx ? sizeof(wl_packet_stream_t) : 0;
  size_t total = sizeof(demo_node_t);
  if (stream_tx) {
    /* WLC emits the same prefix before rx for all three framings. The stream
     * nodes then place tx_write, stream, and tx_wire after their two RX slots. */
    size_t tail = aligned(offsetof(demo_node_t, rx) + rx, _Alignof(demo_tx_write_fn));
    tail += sizeof(demo_tx_write_fn);
    tail = aligned(tail, _Alignof(wl_packet_stream_t)) + stream + stream_tx;
    size_t alignment = _Alignof(demo_node_t);
    if (_Alignof(wl_packet_stream_t) > alignment) alignment = _Alignof(wl_packet_stream_t);
    total = aligned(tail, alignment);
  }
  /* Attribute each byte to one column, including generated node padding. */
  const size_t endpoint = total - port - rx - stream - stream_tx;
  printf(PREFIX ",storage,%s,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n", name,
    endpoint, port, rx, stream, stream_tx,
    byte_storage ? sizeof(wl_byte_fifo_t) : 0, byte_storage);
}
int main(void) {
  printf(PREFIX ",probe,2\n");
  type("wl_endpoint_t", sizeof(wl_endpoint_t)); type("wl_packet_port_t", sizeof(wl_packet_port_t));
  type("wl_packet_stream_t", sizeof(wl_packet_stream_t)); type("wl_byte_fifo_t", sizeof(wl_byte_fifo_t));
  type("demo_node_t", sizeof(demo_node_t)); type("wl_call_handle_t", sizeof(wl_call_handle_t));
  type("wl_packet_loopback_t", sizeof(wl_packet_loopback_t));
  const size_t cobs_rx = wl_packet_stream_cobs_crc16_bound(DEMO_RX_FRAME_MAX);
  const size_t cobs_tx = wl_packet_stream_cobs_crc16_bound(DEMO_TX_FRAME_MAX);
  const size_t length16_rx = wl_packet_stream_length16_none_bound(DEMO_RX_FRAME_MAX);
  const size_t length16_tx = wl_packet_stream_length16_none_bound(DEMO_TX_FRAME_MAX);
  assembly("demo_usb_native_none", DEMO_RX_WIRE_MAX, 0, 0);
  assembly("demo_serial_cobs_crc16", cobs_rx, cobs_tx, 2 * cobs_rx);
  assembly("demo_length16_none", length16_rx, length16_tx, 0);
  return 0;
}
