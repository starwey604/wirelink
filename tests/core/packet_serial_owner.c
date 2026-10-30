/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/byte_fifo.h"
wl_packet_port_t *packet_serial_test_port(void) {
  static wl_packet_port_t p;
  return &p;
}
wl_byte_fifo_t *packet_serial_test_fifo(void) {
  static wl_byte_fifo_t f;
  return &f;
}
wl_packet_stream_t *packet_serial_test_stream(void) {
  static wl_packet_stream_t s;
  return &s;
}
int packet_serial_test_init(wl_packet_sink_fn sink, wl_packet_notify_fn notify, void *user) {
  static uint8_t rx[2][256], wire[256], fifo[17];
  wl_packet_stream_t *s = packet_serial_test_stream();
  wl_packet_port_t *p = packet_serial_test_port();
  wl_packet_config_t pc = {.instance = 9001,
                           .rx_storage = &rx[0][0],
                           .rx_unit_size = 256,
                           .rx_storage_size = sizeof(rx),
                           .submit = wl_packet_stream_cobs_crc16_submit,
                           .notify = wl_packet_stream_notify,
                           .user = s};
  int e = wl_packet_init(p, &pc);
  if (e)
    return e;
  wl_packet_stream_config_t sc = {.port = p,
                                  .tx_storage = {wire, sizeof(wire)},
                                  .maximum_frame_size = 100,
                                  .physical_submit = sink,
                                  .notify = notify,
                                  .user = user};
  e = wl_packet_stream_cobs_crc16_init(s, &sc);
  if (e)
    return e;
  return wl_byte_fifo_init(packet_serial_test_fifo(), (wl_span_t){fifo, sizeof(fifo)}, notify,
                           user);
}
