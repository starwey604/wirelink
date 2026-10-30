/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_BYTE_FIFO_H_
#define WIRELINK_BYTE_FIFO_H_
#include "wirelink/packet_stream.h"
#ifndef __cplusplus
#include <stdatomic.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Independent SPSC byte staging, not a protocol or RPC queue. Producer only
 * reserves/copies/commits and reports gaps; owner parses later. Both roles
 * must stop before init/reuse. Claim/peek capacity is saved internally. */
typedef struct wl_byte_fifo wl_byte_fifo_t;
#ifndef __cplusplus
struct wl_byte_fifo {
  wl_span_t storage;
  wl_packet_notify_fn notify;
  void *user;
  atomic_uint_least32_t read, write;
  atomic_bool gap;
  size_t reserved, peeked;
  uint8_t reservation;
};
#endif
wl_err_t wl_byte_fifo_init(wl_byte_fifo_t *, wl_span_t, wl_packet_notify_fn, void *);
wl_err_t wl_byte_fifo_reserve(wl_byte_fifo_t *, wl_span_t *);
wl_err_t wl_byte_fifo_commit(wl_byte_fifo_t *, size_t);
int wl_byte_fifo_has_space(const wl_byte_fifo_t *);
wl_span_t wl_byte_fifo_peek(wl_byte_fifo_t *);
wl_err_t wl_byte_fifo_consume(wl_byte_fifo_t *, size_t);
int wl_byte_fifo_readable(const wl_byte_fifo_t *);
void wl_byte_fifo_note_gap(wl_byte_fifo_t *);
int wl_byte_fifo_take_gap(wl_byte_fifo_t *);
/* Owner-only COBS/CRC16 service. Errors remain visible while resynchronizing
 * after a known byte loss. Backpressure retains unconsumed FIFO bytes. */
wl_err_t wl_byte_fifo_to_cobs(wl_byte_fifo_t *, wl_packet_stream_t *, unsigned);
int wl_byte_fifo_cobs_ready(const wl_byte_fifo_t *, const wl_packet_stream_t *);
#ifdef __cplusplus
}
#endif
#endif
