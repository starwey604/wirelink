/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_SAMPLE_UDP_NETWORK_H_
#define WIRELINK_SAMPLE_UDP_NETWORK_H_

#include <zephyr/net/net_config.h>
#include <zephyr/sys/printk.h>
#include "wirelink/platform.h"
#include "wirelink/zephyr/udp.h"

/* Sample network provisioning, deliberately outside the adapter. Board/PHY,
 * addresses and entropy must be configured before opening an endpoint. */
static inline int sample_network_init(void) {
  return net_config_init("Wirelink UDP", NET_CONFIG_NEED_IPV4, 30000);
}

static inline wl_err_t sample_udp_open(wl_zephyr_udp_t *udp, wl_endpoint_t *endpoint) {
  const wl_zephyr_udp_config_t network = {
    .peer_address = CONFIG_SAMPLE_WIRELINK_UDP_PEER_ADDRESS,
    .peer_port = CONFIG_SAMPLE_WIRELINK_UDP_PEER_PORT,
    .local_port = CONFIG_SAMPLE_WIRELINK_UDP_LOCAL_PORT,
  };
  return wl_zephyr_udp_open(udp, endpoint, &network);
}
#endif
