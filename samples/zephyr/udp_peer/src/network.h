/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_SAMPLE_UDP_NETWORK_H_
#define WIRELINK_SAMPLE_UDP_NETWORK_H_
#include <zephyr/net/net_config.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>
#include "wirelink/platform.h"
#include "zephyr_owner.h"
/* Board/PHY, IP configuration and a real fresh-instance source are application
 * provisioning. No adapter/core fallback or discovery is implied. */
typedef struct {
  wl_kernel_udp_t adapter;
  wl_packet_port_t port;
  uint8_t rx[2][2053];
} sample_udp_t;
static inline int sample_network_init(void) {
  return net_config_init("Wirelink UDP", NET_CONFIG_NEED_IPV4, 30000);
}
static inline wl_err_t sample_udp_open(sample_udp_t *udp) {
  wl_environment_t environment = wl_platform_environment(); uint64_t instance;
  wl_err_t error = wl_session_next(environment.session, 0, &instance);
  if (error != WL_OK) return error;
  wl_packet_config_t port = {.instance = instance, .rx_storage = &udp->rx[0][0],
    .rx_storage_size = sizeof(udp->rx), .rx_unit_size = sizeof(udp->rx[0]),
    .submit = wl_kernel_udp_submit, .notify = wl_kernel_udp_notify, .user = &udp->adapter};
  error = wl_packet_init(&udp->port, &port);
  if (error != WL_OK) return error;
  wl_kernel_udp_config_t config = {.port = &udp->port, .maximum_datagram_size = 2052,
    .service_budget = 8, .clock = environment.clock,
    .local = {.sin_family = NET_AF_INET, .sin_port = net_htons(CONFIG_SAMPLE_WIRELINK_UDP_LOCAL_PORT)},
    .peer = {.sin_family = NET_AF_INET, .sin_port = net_htons(CONFIG_SAMPLE_WIRELINK_UDP_PEER_PORT)}};
  if (net_addr_pton(NET_AF_INET, CONFIG_SAMPLE_WIRELINK_UDP_PEER_ADDRESS, &config.peer.sin_addr) != 0)
    return WL_ERR_INVALID_ARG;
  return wl_kernel_udp_open(&udp->adapter, &config);
}
#endif
