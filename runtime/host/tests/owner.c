/* SPDX-License-Identifier: Apache-2.0 */
#include "owner.h"
static wl_kernel_t core;
static wl_packet_port_t port;
static uint8_t rx[2][64], tx[WL_KERNEL_TX_COUNT][64];
static wl_err_t route(wl_kernel_t *k, const wl_kernel_frame_t *frame) {
  (void)k; (void)frame; return WL_ERR_NOT_SUPPORTED;
}
wl_kernel_t *executor_test_core(void) { return &core; }
wl_err_t executor_test_init(uint32_t instance, wl_packet_sink_fn sink,
                            wl_packet_notify_fn notify, void *user) {
  wl_packet_config_t config = {.instance = instance, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx),
    .submit = sink, .notify = notify, .user = user};
  wl_err_t error = wl_packet_init(&port, &config);
  if (error != WL_OK) return error;
  wl_kernel_config_t kernel = {.port = &port,
    .storage = wl_kernel_storage_uniform(&tx[0][0], sizeof(tx[0])), .route = route};
  return wl_kernel_init(&core, &kernel);
}
static void callback_marker(void) {}
wl_err_t executor_test_call(uint32_t now, uint32_t timeout,
                            wl_kernel_complete_fn complete, void *user) {
  uint8_t slot;
  uint8_t *payload;
  size_t capacity = 0U;
  wl_err_t error = wl_kernel_call_claim(&core, &slot, &payload, &capacity);
  if (error != WL_OK) return error;
  if (capacity < 1U) return WL_ERR_BUF_TOO_SMALL;
  payload[0] = 77U;
  wl_kernel_call_handle_t call;
  return wl_kernel_call_commit(&core, slot, 40U, 41U, 1U, now, timeout, 1U,
      complete, callback_marker, user, &call);
}
