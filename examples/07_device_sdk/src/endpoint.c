/* SPDX-License-Identifier: Apache-2.0 */
#include "endpoint.h"
#include "device.h"
#include <string.h>
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
  device_node_t* owner = state->endpoint;
  state->notify = notify;
  state->notify_context = context;
  return device_call_configure_ex(owner, state->request, now, timeout,
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
  device_node_t* owner = state->endpoint;
  state->notify = notify;
  state->notify_context = context;
  return device_call_get_info_ex(owner, state->request, now, timeout,
      device_sdk_get_info_done, state, call);
}
