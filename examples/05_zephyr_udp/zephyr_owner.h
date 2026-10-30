/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_EXAMPLE_ZEPHYR_UDP_OWNER_H
#define WIRELINK_EXAMPLE_ZEPHYR_UDP_OWNER_H
#include "wirelink/kernel.h"
#include "wirelink/zephyr/kernel_udp.h"
/* Application support around one existing kernel/UDP adapter, with no stored
 * RPC state. Clock reads belong to this outer owner, not the C core. */
static inline wl_err_t peer_z_step(wl_kernel_udp_t *udp, wl_kernel_t *core, uint32_t now) {
  wl_err_t error = wl_kernel_udp_service(udp, now);
  wl_err_t advanced = wl_kernel_step(core, now, 16);
  return error == WL_OK ? advanced : error;
}
static inline wl_err_t peer_z_wait(wl_kernel_udp_t *udp, wl_kernel_t *core, uint32_t maximum) {
  wl_clock_t clock = udp->config.clock;
  uint32_t now = clock.now_ms(clock.user_data);
  int has_deadline = 0; uint32_t deadline = 0;
  wl_io_readiness_t io = wl_kernel_udp_readiness(udp, now);
  if (wl_kernel_ready(core, now, &has_deadline, &deadline) || io.runnable) return WL_OK;
  if (has_deadline && deadline - now < maximum) maximum = deadline - now;
  if (io.next_deadline_ms < maximum) maximum = io.next_deadline_ms;
  uint32_t elapsed = clock.now_ms(clock.user_data) - now;
  if (elapsed >= maximum) return WL_OK;
  wl_err_t error = wl_kernel_udp_wait(udp, maximum - elapsed);
  return error == WL_ERR_NO_DATA ? WL_OK : error;
}
static inline wl_err_t peer_z_close(wl_kernel_udp_t *udp, wl_kernel_t *core) {
  wl_err_t error = wl_kernel_close(core);
  if (error != WL_OK) return error;
  error = wl_kernel_udp_close(udp);
  if (error != WL_OK) return error;
  error = wl_kernel_step(core, udp->config.clock.now_ms(udp->config.clock.user_data), 16);
  return error != WL_OK ? error : wl_kernel_quiescent(core) ? WL_OK : WL_ERR_BUSY;
}
#endif
