/* SPDX-License-Identifier: Apache-2.0 */
/* C owns atomic port storage and all kernel callback types. */
#include "endpoint.h"
#include "calculator_kernel.h"
#include <string.h>
typedef struct {
  calculator_kernel_t endpoint;
  wl_packet_port_t port;
  uint8_t rx[2][2053];
} calculator_sdk_owner_t;
size_t calculator_sdk_endpoint_size(void) { return sizeof(calculator_sdk_owner_t); }
size_t calculator_sdk_endpoint_alignment(void) { return _Alignof(calculator_sdk_owner_t); }
wl_err_t calculator_sdk_endpoint_prepare(void* storage, wl_host_endpoint_t* descriptor) {
  calculator_sdk_owner_t* owner = storage;
  memset(owner, 0, sizeof(*owner));
  *descriptor = (wl_host_endpoint_t){owner, &owner->endpoint.core, &owner->port, 2052};
  return WL_OK;
}
wl_err_t calculator_sdk_endpoint_initialize(void* storage, uint64_t instance,
    wl_packet_sink_fn sink, wl_packet_notify_fn notify, void* user) {
  calculator_sdk_owner_t* owner = storage;
  wl_packet_config_t config = {.instance = instance, .rx_storage = &owner->rx[0][0],
    .rx_storage_size = sizeof(owner->rx), .rx_unit_size = sizeof(owner->rx[0]),
    .submit = sink, .notify = notify, .user = user};
  const wl_err_t error = wl_packet_init(&owner->port, &config);
  if (error != WL_OK) return error;
  const calculator_kernel_handlers_t handlers = {0};
  return calculator_kernel_init(&owner->endpoint, &owner->port, &handlers, NULL, NULL);
}
static void calculator_sdk_add_done(void* context, wl_kernel_result_t result, const add_response_t* response) {
  calculator_sdk_call_t* state = context;
  if (result.outcome == WL_KERNEL_SUCCESS &&
      (response == NULL || add_response_value_from_view(response, state->response) != WL_CODEC_OK))
    result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  state->notify(state->notify_context, result);
}
wl_err_t calculator_sdk_add_submit(calculator_sdk_call_t* state, uint32_t now, uint32_t timeout,
    wl_host_rpc_notify_fn notify, void* context, wl_kernel_call_handle_t* call) {
  calculator_sdk_owner_t* owner = state->endpoint;
  state->notify = notify;
  state->notify_context = context;
  return calculator_kernel_add_start(&owner->endpoint, state->request, now, timeout,
      calculator_sdk_add_done, state, call);
}
