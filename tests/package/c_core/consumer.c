/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/wirelink.h"
#include <string.h>
static wl_kernel_t endpoint;
static wl_packet_port_t port;
static uint8_t rx[2][64], tx[WL_KERNEL_TX_COUNT][32];
static wl_packet_submit_t submit(void *user, const uint8_t *data, size_t length,
                                 wl_packet_token_t token) {
  (void)user; (void)data; (void)length; (void)token; return WL_PACKET_BUSY;
}
static wl_err_t route(wl_kernel_t *core, const wl_kernel_frame_t *frame) {
  (void)core; (void)frame; return WL_ERR_NOT_SUPPORTED;
}
int main(void) {
  wl_packet_config_t config = {.instance = 17, .rx_storage = &rx[0][0],
      .rx_storage_size = sizeof(rx), .rx_unit_size = sizeof(rx[0]), .submit = submit};
  if (wl_packet_init(&port, &config) != WL_OK) return 1;
  wl_kernel_config_t kernel = {.port = &port, .route = route};
  for (unsigned i = 0; i < WL_KERNEL_TX_COUNT; ++i)
    kernel.tx_storage[i] = (wl_span_t){tx[i], sizeof(tx[i])};
  if (wl_kernel_init(&endpoint, &kernel) != WL_OK) return 2;
  if (wl_kernel_close(&endpoint) != WL_OK || wl_kernel_step(&endpoint, 0, 16) != WL_OK)
    return 3;
  if (!wl_kernel_quiescent(&endpoint)) return 4;
  return strcmp(wl_err_str(WL_OK), "Success (WL_OK)") != 0;
}
