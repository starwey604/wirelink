/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/udp.h"

WL_ZEPHYR_UDP_DEFINE(cpp_adapter, 16, 2);
static_assert(sizeof(cpp_adapter_rx_storage) == 2 * (16 + WL_FRAME_HEADER_SIZE + WL_FRAME_MAX_CRC + 1));

extern "C" int udp_cpp_headers(void) {
  wl_zephyr_udp_stats_t stats{};
  return wl_zephyr_udp_get_stats(&cpp_adapter, &stats) == WL_OK &&
         !stats.common.started && wl_zephyr_udp_local_port(&cpp_adapter) == 0 &&
         wl_zephyr_udp_close(&cpp_adapter) == WL_OK;
}
