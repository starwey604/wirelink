/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_UDP_H_
#define WIRELINK_UDP_H_

#include "wirelink/transport.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wl_udp wl_udp_t;

typedef struct {
  const char *bind_address;        /* NULL binds to 0.0.0.0. */
  uint16_t bind_port;              /* Zero lets the OS choose a port. */
  const char *peer_address;        /* Optional, paired with peer_port. */
  uint16_t peer_port;
  size_t maximum_datagram_size;    /* Zero uses the attached node RX wire bound. */
  size_t maximum_tx_datagram_size; /* Zero uses the attached node TX wire bound. */
  unsigned service_budget;         /* Zero defaults to 8. Valid range is 1..16. */
  bool learn_peer;
  /* Optional owner-side decoder. Updates the length to that of the decoded frame. */
  wl_err_t (*decode)(uint8_t *, size_t, size_t *);
} wl_udp_config_t;

typedef struct {
  uint64_t received;
  uint64_t sent;
  /* Datagrams rejected by the adapter (no peer, invalid size or frame, or
   * wrong source). Once connected, the OS filters new foreign datagrams;
   * only foreign datagrams already queued can count here. */
  uint64_t rejected;
  uint64_t errors;
} wl_udp_stats_t;

/* Open binds the socket immediately. Attach the returned transport to one node.
 * On failure *out is NULL and os_error (when supplied) receives the system
 * error number, or zero for argument and allocation errors. */
wl_err_t wl_udp_open(wl_udp_t **out, const wl_udp_config_t *config, int *os_error);
wl_transport_t *wl_udp_transport(wl_udp_t *udp);
/* Close after the attached node is quiescent and external notifiers have joined. */
void wl_udp_close(wl_udp_t *udp);

/* Owner only. Set a peer before or after binding the node. */
wl_err_t wl_udp_set_peer(wl_udp_t *udp, const char *address, uint16_t port);
uint16_t wl_udp_local_port(const wl_udp_t *udp);
wl_udp_stats_t wl_udp_stats(const wl_udp_t *udp);

#ifdef __cplusplus
}
#endif
#endif
