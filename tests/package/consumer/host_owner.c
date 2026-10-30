/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/kernel.h>
static wl_kernel_t core;
static wl_packet_port_t port;
static uint8_t rx[2][64], tx[9][32];
static wl_packet_submit_t send(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  (void)user; (void)data; (void)length; (void)token; return WL_PACKET_BUSY;
}
static wl_err_t route(wl_kernel_t *kernel, const wl_kernel_frame_t *frame) {
  (void)kernel; (void)frame; return WL_ERR_NOT_SUPPORTED;
}
wl_kernel_t *installed_host_owner(void) {
  wl_packet_config_t packet = {.instance = 81, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx), .submit = send};
  if (wl_packet_init(&port, &packet) != WL_OK) return NULL;
  wl_kernel_config_t config = {.port = &port, .route = route};
  for (unsigned i = 0; i < 9; ++i) config.tx_storage[i] = (wl_span_t){tx[i], sizeof(tx[i])};
  return wl_kernel_init(&core, &config) == WL_OK ? &core : NULL;
}
