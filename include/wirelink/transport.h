/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_TRANSPORT_H_
#define WIRELINK_TRANSPORT_H_
#include "wirelink/io_readiness.h"
#include "wirelink/packet_stream.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { WL_NODE_NATIVE, WL_NODE_COBS_CRC16, WL_NODE_LENGTH16 };

typedef struct wl_transport wl_transport_t;

typedef struct {
  uint8_t *rx_storage;
  size_t rx_storage_size;
  size_t rx_unit_size;
} wl_transport_rx_override_t;

typedef struct {
  wl_packet_port_t *port;
  wl_packet_stream_t *stream; /* Non-NULL only for stream-framed nodes (COBS/length16). */
  size_t rx_wire_max;
  size_t tx_wire_max;
  uint8_t framing;
} wl_transport_attach_t;

typedef struct {
  /* Optional: supply external RX slots before port init (e.g. USB Bulk 64/512B alignment). */
  wl_err_t (*configure_rx)(wl_transport_t *self, size_t min_unit_size,
                           wl_transport_rx_override_t *out);
  /* Bind initialized port/stream and start I/O. Called once by wl_node after port init.
   * Must roll back any partially registered callbacks/resources before returning an error. */
  wl_err_t (*attach)(wl_transport_t *self, const wl_transport_attach_t *info);
  /* TX submission (may be NULL only for recv-only transports). */
  wl_packet_submit_t (*submit)(wl_transport_t *self, const uint8_t *data, size_t length,
                               wl_packet_token_t token);
  /* Thread-safe latched wake. Publish before notify; a notification before wait
   * must also wake the owner. A blocking host executor needs wait and notify together. */
  void (*notify)(wl_transport_t *self);
  /* Optional owner-side pump (NULL for pure IRQ/background-thread transports). */
  wl_err_t (*service)(wl_transport_t *self, uint32_t now_ms);
  /* Optional readiness & blocking wait (NULL for bare-metal polling). */
  wl_io_readiness_t (*readiness)(const wl_transport_t *self, uint32_t now_ms);
  wl_err_t (*wait)(wl_transport_t *self, uint32_t max_ms);
  /* Request stop and report completion:
   * - WL_OK: RX/TX producers stopped and all async callbacks/buffers reclaimed.
   * - WL_ERR_WOULD_BLOCK: stop initiated; owner must keep stepping/retrying close until WL_OK.
   * - Other errors (e.g. WL_ERR_IO): hardware disable failure, propagated to caller. */
  wl_err_t (*stop)(wl_transport_t *self);
} wl_transport_ops_t;

struct wl_transport {
  const wl_transport_ops_t *ops;
};

#ifdef __cplusplus
}
#endif
#endif
