/* SPDX-License-Identifier: Apache-2.0 */
#include "device_kernel.h"
#include "device_kernel_codecs.h"
#include <string.h>
static void device_kernel_complete_configure(wl_kernel_t *core, const wl_kernel_call_t *call, wl_kernel_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  configure_response_t response;
  const configure_response_t *view = NULL;
  if (result.outcome == WL_KERNEL_SUCCESS) {
    if (device_rpc_configure_response_decode(body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((device_kernel_configure_callback_t)call->callback)(call->user, result, view);
}
wl_err_t device_kernel_configure_start(device_kernel_t *endpoint, const configure_request_value_t *request, uint32_t now, uint32_t timeout, device_kernel_configure_callback_t callback, void *user, wl_kernel_call_handle_t *out) {
  if (out != NULL) *out = (wl_kernel_call_handle_t){0};
  if (endpoint == NULL || request == NULL || callback == NULL || out == NULL || timeout == 0U || timeout > INT32_MAX) return WL_ERR_INVALID_ARG;
  uint8_t slot;
  uint8_t *payload;
  wl_err_t error = wl_kernel_call_claim(&endpoint->core, &slot, &payload, NULL);
  if (error != WL_OK) return error;
  size_t length = 0U;
  if (device_rpc_configure_request_value_encode(request, payload, sizeof(endpoint->requests[0]) - WL_KERNEL_NATIVE_HEADER - WL_KERNEL_RPC_HEADER_DATA, &length) != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  return wl_kernel_call_commit(&endpoint->core, slot, 5U, 6U, length, now, timeout, device_kernel_complete_configure, (void (*)(void))callback, user, out);
}
static void device_kernel_complete_get_info(wl_kernel_t *core, const wl_kernel_call_t *call, wl_kernel_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  info_response_t response;
  const info_response_t *view = NULL;
  if (result.outcome == WL_KERNEL_SUCCESS) {
    if (device_rpc_info_response_decode(body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((device_kernel_get_info_callback_t)call->callback)(call->user, result, view);
}
wl_err_t device_kernel_get_info_start(device_kernel_t *endpoint, const info_request_value_t *request, uint32_t now, uint32_t timeout, device_kernel_get_info_callback_t callback, void *user, wl_kernel_call_handle_t *out) {
  if (out != NULL) *out = (wl_kernel_call_handle_t){0};
  if (endpoint == NULL || request == NULL || callback == NULL || out == NULL || timeout == 0U || timeout > INT32_MAX) return WL_ERR_INVALID_ARG;
  uint8_t slot;
  uint8_t *payload;
  wl_err_t error = wl_kernel_call_claim(&endpoint->core, &slot, &payload, NULL);
  if (error != WL_OK) return error;
  size_t length = 0U;
  if (device_rpc_info_request_value_encode(request, payload, sizeof(endpoint->requests[0]) - WL_KERNEL_NATIVE_HEADER - WL_KERNEL_RPC_HEADER_DATA, &length) != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  return wl_kernel_call_commit(&endpoint->core, slot, 2U, 4U, length, now, timeout, device_kernel_complete_get_info, (void (*)(void))callback, user, out);
}
static wl_err_t device_kernel_route(wl_kernel_t *core, const wl_kernel_frame_t *frame) {
  device_kernel_t *endpoint = (device_kernel_t *)core;
  (void)endpoint;
  switch (frame->message_id) {
  case 6U: return wl_kernel_response(core, frame);
  case 5U: {
    if (frame->kind != WL_WIRE_RPC_REQ) return WL_ERR_CORRUPT_PAYLOAD;
    if (endpoint->handlers.on_rpc_configure == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_kernel_rpc_metadata_t metadata;
    wl_err_t error = wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
    if (error != WL_OK) return error;
    uint8_t reply;
    uint8_t *payload;
    error = wl_kernel_reply_claim(core, 6U, &metadata, &reply, &payload, NULL);
    if (error != WL_OK) return error;
    configure_request_t request;
    if (device_rpc_configure_request_decode(metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, reply);
      return WL_ERR_CORRUPT_PAYLOAD;
    }
    device_rpc_configure_response_value_clear(&endpoint->reply.configure);
    int32_t status = endpoint->handlers.on_rpc_configure(endpoint->user, &request, &endpoint->reply.configure);
    if (status != 0) return wl_kernel_reply_error(core, reply, status);
    size_t length = 0U;
    if (device_rpc_configure_response_value_encode(&endpoint->reply.configure, payload, sizeof(endpoint->replies[0]) - WL_KERNEL_NATIVE_HEADER - WL_KERNEL_RPC_HEADER_DATA, &length) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, reply);
      return WL_ERR_CORRUPT_PAYLOAD;
    }
    return wl_kernel_reply_commit(core, reply, length);
  }
  case 4U: return wl_kernel_response(core, frame);
  case 2U: {
    if (frame->kind != WL_WIRE_RPC_REQ) return WL_ERR_CORRUPT_PAYLOAD;
    if (endpoint->handlers.on_rpc_get_info == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_kernel_rpc_metadata_t metadata;
    wl_err_t error = wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
    if (error != WL_OK) return error;
    uint8_t reply;
    uint8_t *payload;
    error = wl_kernel_reply_claim(core, 4U, &metadata, &reply, &payload, NULL);
    if (error != WL_OK) return error;
    info_request_t request;
    if (device_rpc_info_request_decode(metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, reply);
      return WL_ERR_CORRUPT_PAYLOAD;
    }
    device_rpc_info_response_value_clear(&endpoint->reply.get_info);
    int32_t status = endpoint->handlers.on_rpc_get_info(endpoint->user, &request, &endpoint->reply.get_info);
    if (status != 0) return wl_kernel_reply_error(core, reply, status);
    size_t length = 0U;
    if (device_rpc_info_response_value_encode(&endpoint->reply.get_info, payload, sizeof(endpoint->replies[0]) - WL_KERNEL_NATIVE_HEADER - WL_KERNEL_RPC_HEADER_DATA, &length) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, reply);
      return WL_ERR_CORRUPT_PAYLOAD;
    }
    return wl_kernel_reply_commit(core, reply, length);
  }
  default: return WL_ERR_NOT_SUPPORTED;
  }
}
wl_err_t device_kernel_init(device_kernel_t *endpoint, wl_packet_port_t *port, const device_kernel_handlers_t *handlers, wl_kernel_error_fn error, void *user) {
  if (endpoint == NULL || port == NULL || handlers == NULL) return WL_ERR_INVALID_ARG;
  const device_kernel_handlers_t saved = *handlers;
  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->handlers = saved;
  endpoint->user = user;
  wl_kernel_config_t config = {
      .port = port,
      .storage = {
          .requests = &endpoint->requests[0][0],
          .replies = &endpoint->replies[0][0],
          .message = endpoint->message,
          .request_stride = sizeof(endpoint->requests[0]),
          .reply_stride = sizeof(endpoint->replies[0]),
          .message_capacity = sizeof(endpoint->message),
      },
      .route = device_kernel_route,
      .on_error = error,
      .user = user,
  };
  return wl_kernel_init(&endpoint->core, &config);
}
wl_err_t device_kernel_step(device_kernel_t *endpoint, uint32_t now) {
  return endpoint == NULL ? WL_ERR_INVALID_ARG : wl_kernel_step(&endpoint->core, now, WL_KERNEL_BUDGET);
}
