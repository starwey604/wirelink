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
  wl_zephyr_udp_t adapter;
} sample_udp_t;
static inline int sample_network_init(void) {
  return net_config_init("Wirelink UDP", NET_CONFIG_NEED_IPV4, 30000);
}
static inline wl_err_t sample_udp_open(sample_udp_t *udp, wl_packet_port_t *port,
                                       size_t rx_max, size_t tx_max) {
  wl_environment_t environment = wl_platform_environment();
  wl_zephyr_udp_config_t config = {.port = port, .maximum_datagram_size = rx_max,
    .maximum_tx_datagram_size = tx_max,
    .service_budget = 8, .clock = environment.clock,
    .local = {.sin_family = NET_AF_INET, .sin_port = net_htons(CONFIG_SAMPLE_WIRELINK_UDP_LOCAL_PORT)},
    .peer = {.sin_family = NET_AF_INET, .sin_port = net_htons(CONFIG_SAMPLE_WIRELINK_UDP_PEER_PORT)}};
  if (net_addr_pton(NET_AF_INET, CONFIG_SAMPLE_WIRELINK_UDP_PEER_ADDRESS, &config.peer.sin_addr) != 0)
    return WL_ERR_INVALID_ARG;
  return wl_zephyr_udp_open(&udp->adapter, &config);
}
#endif
