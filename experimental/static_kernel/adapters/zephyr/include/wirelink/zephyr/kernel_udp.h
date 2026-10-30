/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_KERNEL_ZEPHYR_UDP_H_
#define WIRELINK_KERNEL_ZEPHYR_UDP_H_
#include "wirelink/packet_port.h"
#include "wirelink/io_readiness.h"
#include "wirelink/clock.h"
#include "wirelink/zephyr/kernel_udp_socket.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Numeric IPv4 and fixed peer. Configure network interfaces separately.
 * Socket I/O runs on owner; external notifying/stopping THREADS must join
 * before close/reuse. No adapter thread or heap is created. */
typedef struct {
  wl_packet_port_t *port;
  struct net_sockaddr_in local, peer;
  size_t maximum_datagram_size;
  unsigned service_budget; /* 1..16 */
  uint32_t retry_ms;       /* 0 selects 1 ms. */
  wl_clock_t clock;
  wl_err_t (*decode)(uint8_t *, size_t, size_t *); /* Optional selected framing. */
} wl_kernel_udp_config_t;
typedef struct {
  wl_kernel_udp_config_t config;
  wl_kernel_udp_socket_state_t io;
  uint32_t retry_due;
  bool retry_pending, more_rx;
  uint64_t received, sent, rejected, errors;
} wl_kernel_udp_t;
wl_err_t wl_kernel_udp_open(wl_kernel_udp_t *, const wl_kernel_udp_config_t *);
wl_packet_submit_t wl_kernel_udp_submit(void *, const uint8_t *, size_t, wl_packet_token_t);
wl_err_t wl_kernel_udp_service(wl_kernel_udp_t *, uint32_t);
wl_io_readiness_t wl_kernel_udp_readiness(const wl_kernel_udp_t *, uint32_t);
/* Caller merges kernel/application deadlines into maximum_ms. Wait performs
 * no receive, core step or business callback; step after return. */
wl_err_t wl_kernel_udp_wait(wl_kernel_udp_t *, uint32_t maximum_ms);
void wl_kernel_udp_notify(void *);
wl_err_t wl_kernel_udp_request_stop(wl_kernel_udp_t *);
wl_err_t wl_kernel_udp_close(wl_kernel_udp_t *);
uint16_t wl_kernel_udp_local_port(const wl_kernel_udp_t *);
#ifdef __cplusplus
}
#endif
#endif
