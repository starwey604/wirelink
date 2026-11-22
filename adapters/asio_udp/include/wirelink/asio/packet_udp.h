/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ASIO_PACKET_UDP_H_
#define WIRELINK_ASIO_PACKET_UDP_H_

#include "wirelink/endpoint.h"
#include "wirelink/packet_port.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#define WL_ASIO_UDP_NOEXCEPT noexcept
#else
#define WL_ASIO_UDP_NOEXCEPT
#endif

typedef struct wl_asio_udp wl_asio_udp_t;

typedef struct {
  const char *bind_address;        /* NULL binds to 0.0.0.0. */
  uint16_t bind_port;              /* Zero lets the OS choose a port. */
  const char *peer_address;        /* Optional, paired with peer_port. */
  uint16_t peer_port;
  size_t maximum_datagram_size;    /* Zero uses the RX unit size minus one. */
  size_t maximum_tx_datagram_size; /* Zero uses the RX limit. */
  unsigned service_budget;         /* Zero defaults to 8. Valid range is 1..16. */
  bool learn_peer;
  /* Optional owner-side decoder. Updates the length to that of the decoded frame. */
  wl_err_t (*decode)(uint8_t *, size_t, size_t *);
} wl_asio_udp_config_t;

typedef struct {
  uint64_t received;
  uint64_t sent;
  uint64_t rejected;
  uint64_t errors;
} wl_asio_udp_stats_t;

/* Create with a stable port address, initialize the port or node with
 * wl_asio_udp_io, then start. Failed starts can be retried. After stopping a
 * started adapter, create a new one to start again. Join external notify
 * producers and drain the endpoint before destroying the adapter or its port. */
wl_asio_udp_t *wl_asio_udp_create(wl_packet_port_t *port);
void wl_asio_udp_destroy(wl_asio_udp_t *udp) WL_ASIO_UDP_NOEXCEPT;
wl_packet_io_t wl_asio_udp_io(wl_asio_udp_t *udp);

wl_err_t wl_asio_udp_start(wl_asio_udp_t *udp, const wl_asio_udp_config_t *config);
wl_err_t wl_asio_udp_set_peer(wl_asio_udp_t *udp, const char *address, uint16_t port);
void wl_asio_udp_stop(wl_asio_udp_t *udp) WL_ASIO_UDP_NOEXCEPT;

/* All socket operations run on the same owner. Only notify may run across
 * threads. Wait returns WL_OK on activity and WL_ERR_NO_DATA on timeout.
 * UINT32_MAX waits indefinitely unless an endpoint deadline is earlier. */
wl_err_t wl_asio_udp_service(wl_asio_udp_t *udp);
bool wl_asio_udp_ready(const wl_asio_udp_t *udp);
wl_err_t wl_asio_udp_wait(wl_asio_udp_t *udp, uint32_t maximum_ms);

/* core must use the bound port. now_ms comes from the caller's monotonic clock.
 * wait_endpoint treats a timeout as WL_OK. close_endpoint closes and stops I/O,
 * then steps the endpoint and checks quiescence before the caller destroys udp. */
wl_err_t wl_asio_udp_step_endpoint(wl_asio_udp_t *udp, wl_endpoint_t *core, uint32_t now_ms);
wl_err_t wl_asio_udp_wait_endpoint(wl_asio_udp_t *udp, wl_endpoint_t *core,
                                  uint32_t now_ms, uint32_t maximum_ms);
wl_err_t wl_asio_udp_close_endpoint(wl_asio_udp_t *udp, wl_endpoint_t *core, uint32_t now_ms);

uint16_t wl_asio_udp_local_port(const wl_asio_udp_t *udp);
wl_asio_udp_stats_t wl_asio_udp_stats(const wl_asio_udp_t *udp);
wl_packet_submit_t wl_asio_udp_submit(void *user, const uint8_t *data, size_t length,
                                    wl_packet_token_t token);
void wl_asio_udp_notify(void *user);

#ifdef __cplusplus
}
#endif
#undef WL_ASIO_UDP_NOEXCEPT
#endif /* WIRELINK_ASIO_PACKET_UDP_H_ */
