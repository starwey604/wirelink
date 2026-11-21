/* SPDX-License-Identifier: Apache-2.0 */
/* C owns atomic port storage and all endpoint callback types. */
#include "endpoint.h"
#include "device.h"
#include <string.h>
typedef struct {
  device_node_t endpoint;
} device_sdk_owner_t;
size_t device_sdk_endpoint_size(void) { return sizeof(device_sdk_owner_t); }
size_t device_sdk_endpoint_alignment(void) { return _Alignof(device_sdk_owner_t); }
wl_err_t device_sdk_endpoint_prepare(void* storage, wl_host_endpoint_t* descriptor) {
  device_sdk_owner_t* owner = storage;
  memset(owner, 0, sizeof(*owner));
  *descriptor = (wl_host_endpoint_t){owner, &owner->endpoint.core, &owner->endpoint.port, DEVICE_RX_WIRE_MAX, DEVICE_TX_WIRE_MAX};
  return WL_OK;
}
wl_err_t device_sdk_endpoint_initialize(void* storage, uint32_t instance,
    wl_packet_sink_fn sink, wl_packet_notify_fn notify, void* user) {
  device_sdk_owner_t* owner = storage;
  const wl_packet_io_t io = {.submit = sink, .notify = notify, .user = user};
  return device_init_io(&owner->endpoint, instance, &io);
}
static void device_sdk_configure_done(void* context, wl_rpc_result_t result, const configure_response_t* response) {
  device_sdk_call_t* state = context;
  if (result.outcome == WL_RPC_SUCCESS) {
    if (response == NULL)
      result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
    else
      memcpy(state->response, response, sizeof(*response));
  }
  state->notify(state->notify_context, result);
}
wl_err_t device_sdk_configure_submit(device_sdk_call_t* state, uint32_t now, uint32_t timeout,
    wl_host_rpc_notify_fn notify, void* context, wl_call_handle_t* call) {
  device_sdk_owner_t* owner = state->endpoint;
  state->notify = notify;
  state->notify_context = context;
  return device_call_configure_ex(&owner->endpoint, state->request, now, timeout,
      device_sdk_configure_done, state, call);
}

static void device_sdk_get_info_done(void* context, wl_rpc_result_t result, const info_response_t* response) {
  device_sdk_call_t* state = context;
  if (result.outcome == WL_RPC_SUCCESS) {
    if (response == NULL)
      result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
    else
      memcpy(state->response, response, sizeof(*response));
  }
  state->notify(state->notify_context, result);
}
wl_err_t device_sdk_get_info_submit(device_sdk_call_t* state, uint32_t now, uint32_t timeout,
    wl_host_rpc_notify_fn notify, void* context, wl_call_handle_t* call) {
  device_sdk_owner_t* owner = state->endpoint;
  state->notify = notify;
  state->notify_context = context;
  return device_call_get_info_ex(&owner->endpoint, state->request, now, timeout,
      device_sdk_get_info_done, state, call);
}
