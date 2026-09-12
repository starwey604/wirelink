/* SPDX-License-Identifier: Apache-2.0 */
/* Explicit manual RPC opt-in. Never release a callback-managed call. */
#ifndef DEVICE_ADVANCED_H
#define DEVICE_ADVANCED_H
#include "device_runtime.h"
#if DEVICE_HAS_DEFAULT_ENDPOINT
/* Internal result bridge; detailed codec/link errors remain in endpoint_result. */
static inline wl_rpc_err_t device_endpoint_configure_record_result(device_endpoint_t *endpoint,
    const device_runtime_result_t *result) {
  if (!endpoint->private_state.stepping ||
      device_runtime_result_ok(&endpoint->private_state.result))
    endpoint->private_state.result = *result;
  if (device_runtime_result_ok(result)) return WL_RPC_OK;
  return result->detail.rpc.rpc_result != WL_RPC_OK ? result->detail.rpc.rpc_result :
      (result->domain == DEVICE_RUNTIME_INVALID_ARGUMENT ? WL_RPC_ERR_INVALID_ARG : WL_RPC_ERR_INVALID_STATE);
}

#if DEVICE_RUNTIME_HAS_RPC_CLIENT
/* A copyable call handle, not a wire ID. Do not inspect private_state. Calls
 * belong to one endpoint incarnation; release only after a terminal result. */
typedef struct {
  struct {
    const device_endpoint_t *owner;
    uint64_t incarnation;
    wl_rpc_client_handle_t handle;
  } private_state;
} device_configure_call_t;

typedef struct {
  wl_rpc_client_state_t state;
  int32_t application_status;
  int32_t link_result;
  wl_rpc_err_t runtime_error;
  bool response_valid;
  /* Borrowed fields, if present in the schema, live until call release. */
  configure_response_t response;
} device_configure_result_t;

/* On success out_call identifies this invocation. On failure it is cleared;
 * endpoint_result retains detailed codec/link diagnostics. */
static inline wl_rpc_err_t device_endpoint_configure_call(device_endpoint_t *endpoint,
    const configure_request_t *request, uint32_t timeout_ms,
    device_configure_call_t *out_call) {
  device_runtime_t *runtime = device_endpoint_runtime(endpoint);
  device_runtime_result_t result;
  wl_rpc_err_t error;
  wl_time_ms_t now_ms;
  if (out_call == NULL) return WL_RPC_ERR_INVALID_ARG;
  memset(out_call, 0, sizeof(*out_call));
  if (runtime == NULL) return WL_RPC_ERR_NOT_INITIALIZED;
  (void)wl_endpoint_now(device_endpoint_handle(endpoint), &now_ms);
  result = device_configure_client_start(wl_endpoint_link(device_endpoint_handle(endpoint)),
      runtime, request, timeout_ms, now_ms);
  error = device_endpoint_configure_record_result(endpoint, &result);
  if (error != WL_RPC_OK) return error;
  error = wl_rpc_client_get_handle(runtime->rpc_client,
      result.detail.rpc.operation_id, &out_call->private_state.handle);
  if (error != WL_RPC_OK) return error;
  out_call->private_state.owner = endpoint;
  out_call->private_state.incarnation = endpoint->private_state.incarnation;
  return WL_RPC_OK;
}

static inline wl_rpc_err_t device_endpoint_configure_call_get(device_endpoint_t *endpoint,
    const device_configure_call_t *call, wl_rpc_client_result_t *result) {
  device_runtime_t *runtime = device_endpoint_runtime(endpoint);
  wl_rpc_err_t error;
  if (call == NULL || result == NULL) return WL_RPC_ERR_INVALID_ARG;
  if (runtime == NULL) return WL_RPC_ERR_NOT_INITIALIZED;
  if (call->private_state.owner != endpoint ||
      call->private_state.incarnation != endpoint->private_state.incarnation)
    return WL_RPC_ERR_NOT_FOUND;
  error = wl_rpc_client_get_by_handle(runtime->rpc_client, &call->private_state.handle, result);
  if (error != WL_RPC_OK) return error;
  return result->request_message_id == 5U && result->response_message_id == 6U
      ? WL_RPC_OK : WL_RPC_ERR_RESPONSE_MISMATCH;
}

/* Pending and terminal states both return RPC_OK. A rejection has a nonzero
 * application_status and no response body; it is not a decode failure. */
static inline wl_rpc_err_t device_endpoint_configure_inspect(device_endpoint_t *endpoint,
    const device_configure_call_t *call, device_configure_result_t *out_result) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t error;
  device_runtime_result_t decoded;
  if (out_result == NULL) return WL_RPC_ERR_INVALID_ARG;
  memset(out_result, 0, sizeof(*out_result));
  error = device_endpoint_configure_call_get(endpoint, call, &client);
  if (error != WL_RPC_OK) return error;
  out_result->state = client.state;
  out_result->application_status = client.application_status;
  out_result->link_result = client.link_result;
  out_result->runtime_error = client.runtime_error;
  if (client.state != WL_RPC_CLIENT_COMPLETED) return WL_RPC_OK;
  decoded = device_configure_client_decode(&client, &out_result->response);
  if (!device_runtime_result_ok(&decoded)) {
    endpoint->private_state.result = decoded;
    return decoded.detail.rpc.rpc_result != WL_RPC_OK ? decoded.detail.rpc.rpc_result : WL_RPC_ERR_INVALID_STATE;
  }
  out_result->response_valid = true;
  return WL_RPC_OK;
}

static inline wl_rpc_err_t device_endpoint_configure_release(device_endpoint_t *endpoint,
    const device_configure_call_t *call) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t error = device_endpoint_configure_call_get(endpoint, call, &client);
  return error == WL_RPC_OK ? wl_rpc_client_release_handle(
      device_endpoint_runtime(endpoint)->rpc_client, &call->private_state.handle) : error;
}

static inline wl_rpc_err_t device_endpoint_configure_cancel(device_endpoint_t *endpoint,
    const device_configure_call_t *call) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t error = device_endpoint_configure_call_get(endpoint, call, &client);
  if (error != WL_RPC_OK) return error;
  error = wl_rpc_client_cancel_handle(device_endpoint_runtime(endpoint)->rpc_client,
      &call->private_state.handle);
  if (error == WL_RPC_OK && client.tx_handle != 0U)
    (void)wl_tx_cancel(wl_endpoint_link(device_endpoint_handle(endpoint)), client.tx_handle);
  return error;
}

#endif

#if DEVICE_RUNTIME_HAS_RPC_SERVER
/* Reply submission returns an RPC error code, not a generic runtime result. */
static inline wl_rpc_err_t device_endpoint_configure_complete(device_endpoint_t *endpoint,
    const device_configure_request_token_t *token, const configure_response_t *response) {
  device_runtime_t *runtime = device_endpoint_runtime(endpoint);
  device_runtime_result_t result;
  wl_time_ms_t now_ms;
  if (runtime == NULL) return WL_RPC_ERR_NOT_INITIALIZED;
  (void)wl_endpoint_now(device_endpoint_handle(endpoint), &now_ms);
  result = device_configure_server_complete(runtime, token, response, now_ms);
  return device_endpoint_configure_record_result(endpoint, &result);
}
static inline wl_rpc_err_t device_endpoint_configure_reject(device_endpoint_t *endpoint,
    const device_configure_request_token_t *token, int32_t application_status) {
  device_runtime_t *runtime = device_endpoint_runtime(endpoint);
  device_runtime_result_t result;
  wl_time_ms_t now_ms;
  if (runtime == NULL) return WL_RPC_ERR_NOT_INITIALIZED;
  (void)wl_endpoint_now(device_endpoint_handle(endpoint), &now_ms);
  result = device_configure_server_reject(runtime, token, application_status, now_ms);
  return device_endpoint_configure_record_result(endpoint, &result);
}
#endif
/* Internal result bridge; detailed codec/link errors remain in endpoint_result. */
static inline wl_rpc_err_t device_endpoint_get_info_record_result(device_endpoint_t *endpoint,
    const device_runtime_result_t *result) {
  if (!endpoint->private_state.stepping ||
      device_runtime_result_ok(&endpoint->private_state.result))
    endpoint->private_state.result = *result;
  if (device_runtime_result_ok(result)) return WL_RPC_OK;
  return result->detail.rpc.rpc_result != WL_RPC_OK ? result->detail.rpc.rpc_result :
      (result->domain == DEVICE_RUNTIME_INVALID_ARGUMENT ? WL_RPC_ERR_INVALID_ARG : WL_RPC_ERR_INVALID_STATE);
}

#if DEVICE_RUNTIME_HAS_RPC_CLIENT
/* A copyable call handle, not a wire ID. Do not inspect private_state. Calls
 * belong to one endpoint incarnation; release only after a terminal result. */
typedef struct {
  struct {
    const device_endpoint_t *owner;
    uint64_t incarnation;
    wl_rpc_client_handle_t handle;
  } private_state;
} device_get_info_call_t;

typedef struct {
  wl_rpc_client_state_t state;
  int32_t application_status;
  int32_t link_result;
  wl_rpc_err_t runtime_error;
  bool response_valid;
  /* Borrowed fields, if present in the schema, live until call release. */
  info_response_t response;
} device_get_info_result_t;

/* On success out_call identifies this invocation. On failure it is cleared;
 * endpoint_result retains detailed codec/link diagnostics. */
static inline wl_rpc_err_t device_endpoint_get_info_call(device_endpoint_t *endpoint,
    const info_request_t *request, uint32_t timeout_ms,
    device_get_info_call_t *out_call) {
  device_runtime_t *runtime = device_endpoint_runtime(endpoint);
  device_runtime_result_t result;
  wl_rpc_err_t error;
  wl_time_ms_t now_ms;
  if (out_call == NULL) return WL_RPC_ERR_INVALID_ARG;
  memset(out_call, 0, sizeof(*out_call));
  if (runtime == NULL) return WL_RPC_ERR_NOT_INITIALIZED;
  (void)wl_endpoint_now(device_endpoint_handle(endpoint), &now_ms);
  result = device_get_info_client_start(wl_endpoint_link(device_endpoint_handle(endpoint)),
      runtime, request, timeout_ms, now_ms);
  error = device_endpoint_get_info_record_result(endpoint, &result);
  if (error != WL_RPC_OK) return error;
  error = wl_rpc_client_get_handle(runtime->rpc_client,
      result.detail.rpc.operation_id, &out_call->private_state.handle);
  if (error != WL_RPC_OK) return error;
  out_call->private_state.owner = endpoint;
  out_call->private_state.incarnation = endpoint->private_state.incarnation;
  return WL_RPC_OK;
}

static inline wl_rpc_err_t device_endpoint_get_info_call_get(device_endpoint_t *endpoint,
    const device_get_info_call_t *call, wl_rpc_client_result_t *result) {
  device_runtime_t *runtime = device_endpoint_runtime(endpoint);
  wl_rpc_err_t error;
  if (call == NULL || result == NULL) return WL_RPC_ERR_INVALID_ARG;
  if (runtime == NULL) return WL_RPC_ERR_NOT_INITIALIZED;
  if (call->private_state.owner != endpoint ||
      call->private_state.incarnation != endpoint->private_state.incarnation)
    return WL_RPC_ERR_NOT_FOUND;
  error = wl_rpc_client_get_by_handle(runtime->rpc_client, &call->private_state.handle, result);
  if (error != WL_RPC_OK) return error;
  return result->request_message_id == 2U && result->response_message_id == 4U
      ? WL_RPC_OK : WL_RPC_ERR_RESPONSE_MISMATCH;
}

/* Pending and terminal states both return RPC_OK. A rejection has a nonzero
 * application_status and no response body; it is not a decode failure. */
static inline wl_rpc_err_t device_endpoint_get_info_inspect(device_endpoint_t *endpoint,
    const device_get_info_call_t *call, device_get_info_result_t *out_result) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t error;
  device_runtime_result_t decoded;
  if (out_result == NULL) return WL_RPC_ERR_INVALID_ARG;
  memset(out_result, 0, sizeof(*out_result));
  error = device_endpoint_get_info_call_get(endpoint, call, &client);
  if (error != WL_RPC_OK) return error;
  out_result->state = client.state;
  out_result->application_status = client.application_status;
  out_result->link_result = client.link_result;
  out_result->runtime_error = client.runtime_error;
  if (client.state != WL_RPC_CLIENT_COMPLETED) return WL_RPC_OK;
  decoded = device_get_info_client_decode(&client, &out_result->response);
  if (!device_runtime_result_ok(&decoded)) {
    endpoint->private_state.result = decoded;
    return decoded.detail.rpc.rpc_result != WL_RPC_OK ? decoded.detail.rpc.rpc_result : WL_RPC_ERR_INVALID_STATE;
  }
  out_result->response_valid = true;
  return WL_RPC_OK;
}

static inline wl_rpc_err_t device_endpoint_get_info_release(device_endpoint_t *endpoint,
    const device_get_info_call_t *call) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t error = device_endpoint_get_info_call_get(endpoint, call, &client);
  return error == WL_RPC_OK ? wl_rpc_client_release_handle(
      device_endpoint_runtime(endpoint)->rpc_client, &call->private_state.handle) : error;
}

static inline wl_rpc_err_t device_endpoint_get_info_cancel(device_endpoint_t *endpoint,
    const device_get_info_call_t *call) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t error = device_endpoint_get_info_call_get(endpoint, call, &client);
  if (error != WL_RPC_OK) return error;
  error = wl_rpc_client_cancel_handle(device_endpoint_runtime(endpoint)->rpc_client,
      &call->private_state.handle);
  if (error == WL_RPC_OK && client.tx_handle != 0U)
    (void)wl_tx_cancel(wl_endpoint_link(device_endpoint_handle(endpoint)), client.tx_handle);
  return error;
}

#endif

#if DEVICE_RUNTIME_HAS_RPC_SERVER
/* Reply submission returns an RPC error code, not a generic runtime result. */
static inline wl_rpc_err_t device_endpoint_get_info_complete(device_endpoint_t *endpoint,
    const device_get_info_request_token_t *token, const info_response_t *response) {
  device_runtime_t *runtime = device_endpoint_runtime(endpoint);
  device_runtime_result_t result;
  wl_time_ms_t now_ms;
  if (runtime == NULL) return WL_RPC_ERR_NOT_INITIALIZED;
  (void)wl_endpoint_now(device_endpoint_handle(endpoint), &now_ms);
  result = device_get_info_server_complete(runtime, token, response, now_ms);
  return device_endpoint_get_info_record_result(endpoint, &result);
}
static inline wl_rpc_err_t device_endpoint_get_info_reject(device_endpoint_t *endpoint,
    const device_get_info_request_token_t *token, int32_t application_status) {
  device_runtime_t *runtime = device_endpoint_runtime(endpoint);
  device_runtime_result_t result;
  wl_time_ms_t now_ms;
  if (runtime == NULL) return WL_RPC_ERR_NOT_INITIALIZED;
  (void)wl_endpoint_now(device_endpoint_handle(endpoint), &now_ms);
  result = device_get_info_server_reject(runtime, token, application_status, now_ms);
  return device_endpoint_get_info_record_result(endpoint, &result);
}
#endif
#endif
#endif
