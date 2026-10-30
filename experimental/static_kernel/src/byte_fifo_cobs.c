/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/byte_fifo.h"
wl_err_t wl_byte_fifo_to_cobs(wl_byte_fifo_t *fifo, wl_packet_stream_t *stream, unsigned budget) {
  wl_err_t result = WL_OK;
  if (budget == 0U) return WL_ERR_INVALID_ARG;
  for (unsigned i = 0U; i < budget; ++i) {
    if (wl_byte_fifo_take_gap(fifo)) {
      (void)wl_packet_stream_abort_rx(stream);
      stream->dropping = 1U; // A gap invalidates the partial COBS window.
      if (result == WL_OK) result = WL_ERR_IO;
    }
    wl_span_t bytes = wl_byte_fifo_peek(fifo);
    if (bytes.length == 0U) break;
    size_t consumed = 0U;
    wl_err_t error = wl_packet_stream_cobs_crc16_feed(stream, bytes.data, bytes.length, &consumed);
    (void)wl_byte_fifo_consume(fifo, consumed);
    if (error != WL_OK && result == WL_OK) result = error;
    if (consumed != bytes.length) break;
  }
  return result;
}
int wl_byte_fifo_cobs_ready(const wl_byte_fifo_t *fifo, const wl_packet_stream_t *stream) {
  return atomic_load_explicit(&fifo->gap, memory_order_acquire) ||
      (wl_byte_fifo_readable(fifo) && (stream->claimed || stream->dropping ||
       wl_packet_rx_available(stream->config.port)));
}
