/* SPDX-License-Identifier: Apache-2.0 */
#include "endpoint.h"
#include "calculator.h"
#include <string.h>
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
  calculator_node_t* owner = state->endpoint;
  state->notify = notify;
  state->notify_context = context;
  return calculator_call_add_ex(owner, state->request, now, timeout,
      calculator_sdk_add_done, state, call);
}
