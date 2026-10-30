/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_stream.h"
#include "wirelink/kernel_wire.h"
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
static wl_packet_port_t port;
static wl_packet_stream_t stream;
static uint8_t rx[2][257], tx[256];
static uint64_t instance = 100;
static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  (void)user; (void)data; (void)length; (void)token; return WL_PACKET_SENT;
}
static void drain(void) {
  wl_packet_borrow_t view;
  while (wl_packet_rx_borrow(&port, &view) == WL_OK) {
    wl_kernel_frame_t frame;
    CHECK(view.length <= 256 && wl_kernel_native_none_read(view.data, view.length, &frame) == WL_OK);
    CHECK(wl_packet_rx_release(&port, &view) == WL_OK);
  }
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > 4096) return 0;
  CHECK(instance != UINT64_MAX);
  wl_packet_config_t config = {.instance = ++instance, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx), .submit = sink};
  CHECK(wl_packet_init(&port, &config) == WL_OK);
  wl_packet_stream_config_t connector = {.port = &port, .tx_storage = {tx, sizeof(tx)},
    .maximum_frame_size = 128, .physical_submit = sink};
  int cobs = !size || !(data[0] & 1);
  CHECK((cobs ? wl_packet_stream_cobs_crc16_init(&stream, &connector) :
    wl_packet_stream_length16_none_init(&stream, &connector)) == WL_OK);
  for (size_t offset = size ? 1 : 0; offset < size;) {
    size_t length = 1 + data[offset] % 32, consumed = 0;
    if (length > size - offset) length = size - offset;
    (void)(cobs ? wl_packet_stream_cobs_crc16_feed(&stream, data + offset, length, &consumed) :
      wl_packet_stream_length16_none_feed(&stream, data + offset, length, &consumed));
    CHECK(consumed <= length); offset += consumed;
    drain();
    if (!consumed) { CHECK(wl_packet_stream_abort_rx(&stream) == WL_OK); ++offset; }
  }
  drain(); CHECK(wl_packet_stream_abort_rx(&stream) == WL_OK);
  wl_packet_stop(&port); CHECK(wl_packet_quiescent(&port)); return 0;
}
