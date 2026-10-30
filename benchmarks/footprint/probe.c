/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include "demo_kernel.h"
#include "wirelink/byte_fifo.h"
#include "wirelink/packet_loopback.h"
#define PREFIX "wirelink_footprint_v2"
static void type(const char *name, size_t size) {
  printf(PREFIX ",type,%s,%zu\n", name, size);
}
static void assembly(const char *name, size_t unit, size_t stream_tx, size_t byte_storage) {
  /* Explicit sample assembly budget. Driver objects and top-level structure
   * padding are excluded; generated endpoint includes its codec scratch/TX. */
  printf(PREFIX ",storage,%s,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n", name,
    sizeof(demo_kernel_t), sizeof(wl_packet_port_t), 2 * unit,
    stream_tx ? sizeof(wl_packet_stream_t) : 0, stream_tx,
    byte_storage ? sizeof(wl_byte_fifo_t) : 0, byte_storage);
}
int main(void) {
  printf(PREFIX ",probe,2\n");
  type("wl_kernel_t", sizeof(wl_kernel_t)); type("wl_packet_port_t", sizeof(wl_packet_port_t));
  type("wl_packet_stream_t", sizeof(wl_packet_stream_t)); type("wl_byte_fifo_t", sizeof(wl_byte_fifo_t));
  type("demo_kernel_t", sizeof(demo_kernel_t)); type("wl_kernel_call_handle_t", sizeof(wl_kernel_call_handle_t));
  type("wl_packet_loopback_t", sizeof(wl_packet_loopback_t));
  const size_t cobs = wl_packet_stream_cobs_crc16_bound(DEMO_KERNEL_PACKET_FRAME_MAX);
  const size_t length16 = wl_packet_stream_length16_none_bound(DEMO_KERNEL_PACKET_FRAME_MAX);
  assembly("demo_usb_native_none", DEMO_KERNEL_PACKET_FRAME_MAX + 1, 0, 0);
  assembly("demo_serial_cobs_crc16", cobs, cobs, 2 * cobs);
  assembly("demo_length16_none", length16, length16, 0);
  return 0;
}
