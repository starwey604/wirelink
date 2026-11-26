/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_NODE_H_
#define WIRELINK_ZEPHYR_NODE_H_

#include "wirelink/node.h"
#include <zephyr/kernel.h>

static inline wl_time_ms_t wl_zephyr_now_ms(void *user) {
  (void)user;
  return k_uptime_get_32();
}

static inline wl_clock_t wl_zephyr_clock(void) {
  wl_clock_t clock = {wl_zephyr_now_ms, NULL};
  return clock;
}

static inline wl_err_t wl_zephyr_node_poll(wl_node_t *node, uint32_t timeout_ms) {
  return wl_node_poll(node, wl_zephyr_clock(), timeout_ms);
}

/* Keep node and transport storage alive on timeout. A later close can finish. */
static inline wl_err_t wl_zephyr_node_close(wl_node_t *node, uint32_t timeout_ms) {
  const uint32_t start = k_uptime_get_32();
  for (;;) {
    const wl_err_t err = wl_node_close(node, k_uptime_get_32());
    if (wl_node_quiescent(node)) return WL_OK;
    if (err != WL_OK && err != WL_ERR_BUSY && err != WL_ERR_WOULD_BLOCK) return err;
    if (k_uptime_get_32() - start >= timeout_ms) return WL_ERR_TIMEOUT;
    k_msleep(1);
  }
}
#endif
