/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_DETAIL_UDP_SOCKET_H_
#define WIRELINK_ZEPHYR_DETAIL_UDP_SOCKET_H_

#include <stdbool.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/atomic.h>

/* Layout only, shared with the private socket implementation. Not application
 * API: no field may be accessed or copied while the adapter is in use. */
typedef struct {
  int socket_fd;
  int wake_fd;
  struct net_sockaddr_in peer;
  atomic_t stopped;
  bool opened;
} wl_zephyr_udp_socket_state_t;

#endif
