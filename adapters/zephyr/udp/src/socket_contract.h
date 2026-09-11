/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_UDP_SOCKET_CONTRACT_H_
#define WIRELINK_ZEPHYR_UDP_SOCKET_CONTRACT_H_

/* M0 private building block, not an installed/public adapter API.
 * One owner performs open/receive/send/wait/close. Producer THREADS may notify
 * or request_stop while open, but must join before close or storage reuse.
 * No function parses Wirelink frames or dispatches application callbacks. */
#include "wirelink/zephyr/detail/udp_socket.h"
#include "wirelink/port.h"

typedef wl_zephyr_udp_socket_state_t wl_udp_socket_t;

#define WL_UDP_SOCKET_INITIALIZER { .socket_fd = -1, .wake_fd = -1 }

/* Numeric IPv4 only; no discovery, first-packet learning or IP configuration.
 * Both addresses remain unchanged until close. Local port zero is allowed. */
wl_err_t wl_udp_socket_open(wl_udp_socket_t *io,
    const struct net_sockaddr_in *local, const struct net_sockaddr_in *peer);
/* Idempotent; owner only, AFTER all notifying/stopping threads have joined. */
wl_err_t wl_udp_socket_close(wl_udp_socket_t *io);
wl_err_t wl_udp_socket_notify(wl_udp_socket_t *io);
wl_err_t wl_udp_socket_request_stop(wl_udp_socket_t *io);
/* receive_ready=false masks level-triggered RX while core slots are full.
 * UINT32_MAX means unbounded. Temporary TX pressure is NOT a POLLOUT loop:
 * the future adapter supplies a bounded retry deadline through maximum_ms. */
wl_err_t wl_udp_socket_wait(wl_udp_socket_t *io, bool receive_ready,
                           uint32_t maximum_ms);
/* Receive directly into caller's claim, with ONE guard byte past maximum_frame.
 * Only WL_OK publishes a length. All other outcomes require claim abort.
 * Empty/foreign packets: BAD_FRAME; oversized/truncated: FRAME_TOO_LONG.
 * This function consumes at most one datagram, including rejected packets. */
wl_err_t wl_udp_socket_receive(wl_udp_socket_t *io, uint8_t *buffer,
    size_t capacity, size_t maximum_frame, size_t *received);
/* SENT means the socket accepted the entire datagram, NOT MAC completion or
 * peer receipt. Never retain buffer or create an adapter-side retry queue. */
wl_sink_result_t wl_udp_socket_send(wl_udp_socket_t *io,
                                   const uint8_t *buffer, size_t length);
/* Pure syscall-result classification, shared with deterministic error tests. */
wl_sink_result_t wl_udp_socket_classify_send(ssize_t result, size_t length,
                                            int socket_error);

#endif
