/* SPDX-License-Identifier: Apache-2.0 */
#include "device_kernel.h"
#include "device_kernel_codecs.h"
#include <string.h>
static wl_err_t device_kernel_encode_configure_request(const void *value, uint8_t *data, size_t capacity, size_t *length) {
  return device_rpc_configure_request_value_encode(value, data, capacity, length) == WL_CODEC_OK ? WL_OK : WL_ERR_CORRUPT_PAYLOAD;
}
static wl_err_t device_kernel_encode_configure_response(const void *value, uint8_t *data, size_t capacity, size_t *length) {
  return device_rpc_configure_response_value_encode(value, data, capacity, length) == WL_CODEC_OK ? WL_OK : WL_ERR_CORRUPT_PAYLOAD;
}
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
  if (endpoint == NULL || request == NULL) return WL_ERR_INVALID_ARG;
  return wl_kernel_call_start(&endpoint->core, 5U, 6U, request, device_kernel_encode_configure_request, now, timeout, device_kernel_complete_configure, (void (*)(void))callback, user, out);
}
static wl_err_t device_kernel_encode_info_request(const void *value, uint8_t *data, size_t capacity, size_t *length) {
  return device_rpc_info_request_value_encode(value, data, capacity, length) == WL_CODEC_OK ? WL_OK : WL_ERR_CORRUPT_PAYLOAD;
}
static wl_err_t device_kernel_encode_info_response(const void *value, uint8_t *data, size_t capacity, size_t *length) {
  return device_rpc_info_response_value_encode(value, data, capacity, length) == WL_CODEC_OK ? WL_OK : WL_ERR_CORRUPT_PAYLOAD;
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
  if (endpoint == NULL || request == NULL) return WL_ERR_INVALID_ARG;
  return wl_kernel_call_start(&endpoint->core, 2U, 4U, request, device_kernel_encode_info_request, now, timeout, device_kernel_complete_get_info, (void (*)(void))callback, user, out);
}
static wl_err_t device_kernel_route(wl_kernel_t *core, const wl_kernel_frame_t *frame) {
  device_kernel_t *endpoint = (device_kernel_t *)core;
  (void)endpoint;
  switch (frame->message_id) {
  case 6U: return wl_kernel_response(core, frame->message_id, frame->payload, frame->length);
  case 5U: {
    if (endpoint->handlers.on_rpc_configure == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_kernel_rpc_metadata_t metadata;
    wl_err_t error = wl_kernel_rpc_read(frame->payload, frame->length, 1, &metadata);
    if (error != WL_OK) return error;
    unsigned reply;
    error = wl_kernel_reply_reserve(core, &reply);
    if (error != WL_OK) return error;
    configure_request_t request;
    if (device_rpc_configure_request_decode(metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, reply);
      return WL_ERR_CORRUPT_PAYLOAD;
    }
    device_rpc_configure_response_value_clear(&endpoint->reply.configure);
    int32_t status = endpoint->handlers.on_rpc_configure(endpoint->user, &request, &endpoint->reply.configure);
    return wl_kernel_reply_finish(core, reply, 6U, &metadata, status, &endpoint->reply.configure, device_kernel_encode_configure_response);
  }
  case 4U: return wl_kernel_response(core, frame->message_id, frame->payload, frame->length);
  case 2U: {
    if (endpoint->handlers.on_rpc_get_info == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_kernel_rpc_metadata_t metadata;
    wl_err_t error = wl_kernel_rpc_read(frame->payload, frame->length, 1, &metadata);
    if (error != WL_OK) return error;
    unsigned reply;
    error = wl_kernel_reply_reserve(core, &reply);
    if (error != WL_OK) return error;
    info_request_t request;
    if (device_rpc_info_request_decode(metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, reply);
      return WL_ERR_CORRUPT_PAYLOAD;
    }
    device_rpc_info_response_value_clear(&endpoint->reply.get_info);
    int32_t status = endpoint->handlers.on_rpc_get_info(endpoint->user, &request, &endpoint->reply.get_info);
    return wl_kernel_reply_finish(core, reply, 4U, &metadata, status, &endpoint->reply.get_info, device_kernel_encode_info_response);
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
  wl_kernel_config_t config = {.port = port, .route = device_kernel_route, .on_error = error, .user = user};
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i) config.tx_storage[i] = (wl_span_t){endpoint->requests[i], sizeof(endpoint->requests[i])};
  for (unsigned i = 0U; i < WL_KERNEL_REPLIES; ++i) config.tx_storage[i + WL_KERNEL_CALLS] = (wl_span_t){endpoint->replies[i], sizeof(endpoint->replies[i])};
  config.tx_storage[WL_KERNEL_TX_COUNT - 1U] = (wl_span_t){endpoint->message, sizeof(endpoint->message)};
  return wl_kernel_init(&endpoint->core, &config);
}
wl_err_t device_kernel_step(device_kernel_t *endpoint, uint32_t now) {
  return endpoint == NULL ? WL_ERR_INVALID_ARG : wl_kernel_step(&endpoint->core, now, WL_KERNEL_BUDGET);
}
