/* SPDX-License-Identifier: Apache-2.0 */
// Endpoint callbacks are compiled in C with exactly the runtime's C types.
#include "endpoint.h"
#include "device_endpoint.h"
#include <wirelink/platform.h>
#include <string.h>
size_t device_sdk_endpoint_size(void) { return sizeof(device_endpoint_t); }
size_t device_sdk_endpoint_alignment(void) { return _Alignof(device_endpoint_t); }
wl_err_t device_sdk_endpoint_init(void* storage, wl_endpoint_driver_t* driver) {
  device_endpoint_t* endpoint = storage;
  memset(endpoint, 0, sizeof(*endpoint));
  const wl_err_t status = device_endpoint_init(endpoint, wl_platform_environment());
  if (status == WL_OK) *driver = device_endpoint_driver(endpoint);
  return status;
}
void device_sdk_endpoint_close(void* storage) { (void)device_endpoint_close(storage); }
wl_err_t device_sdk_cancel(void* endpoint, const wl_rpc_call_t* call) {
  return device_endpoint_cancel(endpoint, call);
}
wl_rpc_completion_t device_sdk_configure(void* endpoint, const void* request, void* response, uint32_t timeout_ms) {
  return device_endpoint_configure_sync(endpoint, request, response, timeout_ms);
}
wl_rpc_completion_t device_sdk_get_info(void* endpoint, const void* request, void* response, uint32_t timeout_ms) {
  return device_endpoint_get_info_sync(endpoint, request, response, timeout_ms);
}
static void device_sdk_configure_done(void* context, const wl_rpc_completion_t* result, const configure_response_value_t* response) {
  device_sdk_call_t* state = context;
  if (response != NULL) memcpy(state->response, response, sizeof(*response));
  state->notify(state->notify_context, result);
}
wl_err_t device_sdk_configure_submit(device_sdk_call_t* state, wl_time_ms_t deadline, wl_rpc_sync_notify_fn notify, void* context, wl_rpc_call_t* call) {
  wl_time_ms_t now;
  const int error = wl_endpoint_now(device_endpoint_handle(state->endpoint), &now);
  if (error != WL_OK) return error;
  const uint32_t remaining = deadline - now;
  if (remaining == 0U || remaining > INT32_MAX) return WL_ERR_TIMEOUT;
  state->notify = notify;
  state->notify_context = context;
  return device_endpoint_configure_submit_at(state->endpoint, state->request, remaining, now,
      device_sdk_configure_done, state, call);
}
static void device_sdk_get_info_done(void* context, const wl_rpc_completion_t* result, const info_response_value_t* response) {
  device_sdk_call_t* state = context;
  if (response != NULL) memcpy(state->response, response, sizeof(*response));
  state->notify(state->notify_context, result);
}
wl_err_t device_sdk_get_info_submit(device_sdk_call_t* state, wl_time_ms_t deadline, wl_rpc_sync_notify_fn notify, void* context, wl_rpc_call_t* call) {
  wl_time_ms_t now;
  const int error = wl_endpoint_now(device_endpoint_handle(state->endpoint), &now);
  if (error != WL_OK) return error;
  const uint32_t remaining = deadline - now;
  if (remaining == 0U || remaining > INT32_MAX) return WL_ERR_TIMEOUT;
  state->notify = notify;
  state->notify_context = context;
  return device_endpoint_get_info_submit_at(state->endpoint, state->request, remaining, now,
      device_sdk_get_info_done, state, call);
}
