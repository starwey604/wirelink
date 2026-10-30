/* SPDX-License-Identifier: Apache-2.0 */
#include "owner.h"
static wl_kernel_t core;
static wl_packet_port_t port;
static uint8_t rx[2][64], tx[WL_KERNEL_TX_COUNT][64];
static wl_err_t route(wl_kernel_t *k, const wl_kernel_frame_t *frame) {
  (void)k; (void)frame; return WL_ERR_NOT_SUPPORTED;
}
static wl_err_t encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  if (capacity < 1) return WL_ERR_BUF_TOO_SMALL;
  out[0] = *(const uint8_t *)value; *length = 1; return WL_OK;
}
wl_kernel_t *executor_test_core(void) { return &core; }
wl_err_t executor_test_init(uint64_t instance, wl_packet_sink_fn sink,
                            wl_packet_notify_fn notify, void *user) {
  wl_packet_config_t config = {.instance = instance, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx),
    .submit = sink, .notify = notify, .user = user};
  wl_err_t error = wl_packet_init(&port, &config);
  if (error != WL_OK) return error;
  wl_kernel_config_t kernel = {.port = &port, .route = route};
  for (unsigned i = 0; i < WL_KERNEL_TX_COUNT; ++i)
    kernel.tx_storage[i] = (wl_span_t){tx[i], sizeof(tx[i])};
  return wl_kernel_init(&core, &kernel);
}
static void callback_marker(void) {}
wl_err_t executor_test_call(uint32_t now, uint32_t timeout,
                            wl_kernel_complete_fn complete, void *user) {
  const uint8_t request = 77;
  wl_kernel_call_handle_t call;
  return wl_kernel_call_start(&core, 40, 41, &request, encode, now, timeout,
      complete, callback_marker, user, &call);
}
