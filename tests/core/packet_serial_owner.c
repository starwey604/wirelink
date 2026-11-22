/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/byte_fifo.h"

const size_t *packet_serial_fifo_layout(void) {
  static const size_t layout[] = {
      sizeof(wl_byte_fifo_t), _Alignof(wl_byte_fifo_t), offsetof(wl_byte_fifo_t, read),
      offsetof(wl_byte_fifo_t, write), offsetof(wl_byte_fifo_t, gap),
      offsetof(wl_byte_fifo_t, reserved), offsetof(wl_byte_fifo_t, reservation)};
  return layout;
}
