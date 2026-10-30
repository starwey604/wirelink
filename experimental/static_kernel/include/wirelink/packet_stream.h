/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_PACKET_STREAM_H_
#define WIRELINK_PACKET_STREAM_H_
#include "wirelink/packet_port.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Stream transport outside the native packet kernel. Selected entry points
 * contain only COBS/CRC16 or length16/NONE. No ARQ or execution engine.
 * RX feed is single producer; TX submit is single owner. Stop producer before
 * abort_rx/reinit. Physical completion publishes the original port token;
 * tx_storage and this object remain live through physical drain/stop ack. */
typedef struct {
  wl_packet_port_t *port;
  wl_span_t tx_storage;
  wl_packet_sink_fn physical_submit;
  wl_packet_notify_fn notify;
  void *user;
  size_t maximum_frame_size; /* Canonical ordinary DATA, including four-byte header. */
} wl_packet_stream_config_t;
typedef struct {
  wl_packet_stream_config_t config;
  wl_packet_claim_t claim;
  size_t rx_length;
  size_t expected;
  size_t discard;
  unsigned prefix_bytes;
  uint8_t claimed;
  uint8_t dropping;
} wl_packet_stream_t;
size_t wl_packet_stream_cobs_crc16_bound(size_t canonical_size);
size_t wl_packet_stream_length16_none_bound(size_t canonical_size);
wl_err_t wl_packet_stream_cobs_crc16_init(wl_packet_stream_t *, const wl_packet_stream_config_t *);
wl_err_t wl_packet_stream_length16_none_init(wl_packet_stream_t *, const wl_packet_stream_config_t *);
wl_packet_submit_t wl_packet_stream_cobs_crc16_submit(void *, const uint8_t *, size_t, wl_packet_token_t);
wl_packet_submit_t wl_packet_stream_length16_none_submit(void *, const uint8_t *, size_t, wl_packet_token_t);
/* On backpressure consumes only the prefix belonging to existing RX storage.
 * Caller retains the suffix and retries after owner releases an RX unit.
 * Malformed units are discarded, first error returned, following units resume.
 * Empty input is allowed. The consumed count is always initialized. */
wl_err_t wl_packet_stream_cobs_crc16_feed(wl_packet_stream_t *, const uint8_t *, size_t, size_t *);
wl_err_t wl_packet_stream_length16_none_feed(wl_packet_stream_t *, const uint8_t *, size_t, size_t *);
void wl_packet_stream_notify(void *);
wl_err_t wl_packet_stream_abort_rx(wl_packet_stream_t *);
#ifdef __cplusplus
}
#endif
#endif
