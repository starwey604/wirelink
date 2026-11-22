/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_PACKET_LOOPBACK_H_
#define WIRELINK_PACKET_LOOPBACK_H_
#include "wirelink/transport.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Both endpoints and this in-process adapter share one owner. Bind one native node
 * to each side. Closing either side stops the cable. */
typedef struct wl_packet_loopback wl_packet_loopback_t;
typedef struct {
  wl_transport_t transport;
  wl_packet_loopback_t *owner;
  const uint8_t *data;
  size_t length;
  wl_packet_token_t token;
  uint64_t delivered;
  uint8_t side, pending;
} wl_packet_loopback_direction_t;
struct wl_packet_loopback {
  wl_packet_port_t *ports[2];
  wl_packet_loopback_direction_t directions[2];
  wl_packet_notify_fn notify;
  void *user;
  uint8_t stopped, cursor;
};
wl_err_t wl_packet_loopback_init(wl_packet_loopback_t *, wl_packet_notify_fn, void *);
wl_transport_t *wl_packet_loopback_transport(wl_packet_loopback_t *, unsigned side);
wl_err_t wl_packet_loopback_service(wl_packet_loopback_t *, unsigned budget);
int wl_packet_loopback_ready(const wl_packet_loopback_t *);
void wl_packet_loopback_stop(wl_packet_loopback_t *);
#ifdef __cplusplus
}
#endif
#endif
