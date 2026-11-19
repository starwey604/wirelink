/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/endpoint.h>
static wl_endpoint_t core;
static wl_packet_port_t port;
static uint8_t rx[2][64], tx[WL_ENDPOINT_TX_COUNT][32];
static wl_packet_submit_t send(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  (void)user; (void)data; (void)length; (void)token; return WL_PACKET_BUSY;
}
static wl_err_t route(wl_endpoint_t *endpoint, const wl_wire_frame_t *frame) {
  (void)endpoint; (void)frame; return WL_ERR_NOT_SUPPORTED;
}
wl_endpoint_t *installed_host_owner(void) {
  wl_packet_config_t packet = {.instance = 81, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx), .submit = send};
  if (wl_packet_init(&port, &packet) != WL_OK) return NULL;
  wl_endpoint_config_t config = {.port = &port,
      .storage = wl_endpoint_storage_uniform(&tx[0][0], sizeof(tx[0])),
      .route = route};
  return wl_endpoint_init(&core, &config) == WL_OK ? &core : NULL;
}
