/* SPDX-License-Identifier: Apache-2.0 */
/* C owns atomic port storage and all endpoint callback types. */
#include "endpoint.h"
#include "calculator.h"
#include <string.h>
typedef struct {
  calculator_node_t endpoint;
} calculator_sdk_owner_t;
size_t calculator_sdk_endpoint_size(void) { return sizeof(calculator_sdk_owner_t); }
size_t calculator_sdk_endpoint_alignment(void) { return _Alignof(calculator_sdk_owner_t); }
wl_err_t calculator_sdk_endpoint_prepare(void* storage, wl_host_endpoint_t* descriptor) {
  calculator_sdk_owner_t* owner = storage;
  memset(owner, 0, sizeof(*owner));
  *descriptor = (wl_host_endpoint_t){owner, &owner->endpoint.core, &owner->endpoint.port, CALCULATOR_RX_WIRE_MAX, CALCULATOR_TX_WIRE_MAX};
  return WL_OK;
}
wl_err_t calculator_sdk_endpoint_initialize(void* storage, uint32_t instance,
    wl_packet_sink_fn sink, wl_packet_notify_fn notify, void* user) {
  calculator_sdk_owner_t* owner = storage;
  const wl_packet_io_t io = {.submit = sink, .notify = notify, .user = user};
  return calculator_init_io(&owner->endpoint, instance, &io);
}
static void calculator_sdk_add_done(void* context, wl_rpc_result_t result, const add_response_t* response) {
  calculator_sdk_call_t* state = context;
  if (result.outcome == WL_RPC_SUCCESS) {
    if (response == NULL)
      result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
    else
      memcpy(state->response, response, sizeof(*response));
  }
  state->notify(state->notify_context, result);
}
wl_err_t calculator_sdk_add_submit(calculator_sdk_call_t* state, uint32_t now, uint32_t timeout,
    wl_host_rpc_notify_fn notify, void* context, wl_call_handle_t* call) {
  calculator_sdk_owner_t* owner = state->endpoint;
  state->notify = notify;
  state->notify_context = context;
  return calculator_call_add_ex(&owner->endpoint, state->request, now, timeout,
      calculator_sdk_add_done, state, call);
}
