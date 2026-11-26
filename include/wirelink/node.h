/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_NODE_H_
#define WIRELINK_NODE_H_
#include "wirelink/clock.h"
#include "wirelink/endpoint.h"
#include "wirelink/transport.h"
#ifndef WL_NODE_BUFFER_ALIGN
#define WL_NODE_BUFFER_ALIGN 4U
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wl_node wl_node_t;
typedef wl_err_t (*wl_node_sync_tx_fn)(wl_node_t *node, const uint8_t *data, size_t length);

/* Supplied by WLC only for stream-framed nodes; NULL for native-only builds. */
typedef struct {
  wl_err_t (*init)(wl_packet_stream_t *, const wl_packet_stream_config_t *);
  wl_packet_submit_t (*submit)(void *, const uint8_t *, size_t, wl_packet_token_t);
  wl_err_t (*feed)(wl_packet_stream_t *, const uint8_t *, size_t, size_t *);
  wl_err_t (*abort_rx)(wl_packet_stream_t *);
  wl_packet_notify_fn notify;
  uint8_t framing;
} wl_node_stream_ops_t;

struct wl_node {
  wl_endpoint_t core;
  wl_packet_port_t port;
  wl_transport_t *transport;
  wl_packet_stream_t *stream;
  const wl_node_stream_ops_t *stream_ops;
  wl_node_sync_tx_fn sync_tx;
  void *user;
  wl_endpoint_error_fn on_error;
  uint16_t rx_wire_max;
  uint16_t tx_wire_max;
  uint8_t transport_stopped; /* 1 when no transport or transport->ops->stop returned WL_OK */
};

/* Default owner loop: wait for work, then sample the clock and step once.
 * A zero wait limit skips the wait. A wait error returns before the step. */
wl_err_t wl_node_poll(wl_node_t *node, wl_clock_t clock, uint32_t max_wait_ms);
/* For custom scheduling, resample the owner clock between step and wait. */
wl_err_t wl_node_step(wl_node_t *node, uint32_t now_ms);
wl_io_readiness_t wl_node_readiness(const wl_node_t *node, uint32_t now_ms);
wl_err_t wl_node_wait(wl_node_t *node, uint32_t now_ms, uint32_t max_wait_ms);
/* Retry close on WOULD_BLOCK (driver stopping) or BUSY (leases draining). */
wl_err_t wl_node_close(wl_node_t *node, uint32_t now_ms);
int wl_node_quiescent(const wl_node_t *node);
void wl_node_tx_ready(wl_node_t *node);
wl_err_t wl_node_cancel(wl_node_t *node, wl_call_handle_t handle);

/* Single RX producer. Stop it before closing a synchronously bound node. */
wl_err_t wl_node_rx_packet(wl_node_t *node, const uint8_t *data, size_t length);
wl_err_t wl_node_rx_feed(wl_node_t *node, const uint8_t *data, size_t length, size_t *consumed);

/* Generated, fixed-storage assembly. */
typedef struct {
  uint32_t instance; /* 0 = automatic */
  wl_endpoint_storage_t storage;
  wl_endpoint_route_fn route;
  uint8_t *rx_storage;
  size_t rx_storage_size;
  uint16_t rx_frame_max; /* Protocol-level RX cap (enforced even with larger USB RX slots) */
  uint16_t tx_frame_max;
  uint16_t rx_wire_max;  /* Physical wire RX minimum bound (slot >= rx_wire_max + 1) */
  uint16_t tx_wire_max;
  uint8_t has_tx;
  uint8_t rx_slots; /* 0 selects the default packet RX depth. */
  wl_packet_stream_t *stream;
  const wl_node_stream_ops_t *stream_ops; /* NULL for native framing */
  wl_span_t stream_tx_wire;
  void *user;
  wl_endpoint_error_fn on_error;
  uint16_t rx_unit_size; /* Physical slot stride; 0 selects rx_wire_max + 1. */
} wl_node_layout_t;

wl_err_t wl_node_bind_transport(wl_node_t *node, const wl_node_layout_t *layout,
                                wl_transport_t *transport);
wl_err_t wl_node_bind_sync(wl_node_t *node, const wl_node_layout_t *layout,
                           wl_node_sync_tx_fn tx_write);

#ifdef __cplusplus
}
#endif
#endif
