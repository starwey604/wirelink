/* SPDX-License-Identifier: Apache-2.0 */
#include "demo_kernel.h"
#include <string.h>
static void demo_kernel_complete_execute(wl_kernel_t *core, const wl_kernel_call_t *call, wl_kernel_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  response_t response;
  const response_t *view = NULL;
  if (result.outcome == WL_KERNEL_SUCCESS) {
    if (response_decode(body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((demo_kernel_execute_callback_t)call->callback)(call->user, result, view);
}
wl_err_t demo_kernel_execute_start(demo_kernel_t *endpoint, const request_t *request, uint32_t now, uint32_t timeout, demo_kernel_execute_callback_t callback, void *user, wl_kernel_call_handle_t *out) {
  if (out != NULL) *out = (wl_kernel_call_handle_t){0};
  if (endpoint == NULL || request == NULL || callback == NULL || out == NULL || timeout == 0U || timeout > INT32_MAX) return WL_ERR_INVALID_ARG;
  uint8_t slot;
  uint8_t *payload;
  wl_err_t error = wl_kernel_call_claim(&endpoint->core, &slot, &payload, NULL);
  if (error != WL_OK) return error;
  size_t length = 0U;
  if (request_encode(request, payload, sizeof(endpoint->requests[0]) - WL_KERNEL_NATIVE_HEADER - WL_KERNEL_RPC_HEADER_DATA, &length) != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  return wl_kernel_call_commit(&endpoint->core, slot, 40U, 41U, length, now, timeout, demo_kernel_complete_execute, (void (*)(void))callback, user, out);
}
wl_err_t demo_kernel_send_event(demo_kernel_t *endpoint, const event_t *value) {
  if (endpoint == NULL || value == NULL) return WL_ERR_INVALID_ARG;
  uint8_t *frame;
  wl_err_t error = wl_kernel_msg_claim(&endpoint->core, &frame, NULL);
  if (error != WL_OK) return error;
  size_t length = 0U;
  if (event_encode(value, frame + WL_KERNEL_NATIVE_HEADER, sizeof(endpoint->message) - WL_KERNEL_NATIVE_HEADER, &length) != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  error = wl_wire_frame_seal(frame, sizeof(endpoint->message), WL_WIRE_MSG, 42U, length);
  return error != WL_OK ? error : wl_kernel_msg_commit(&endpoint->core, WL_KERNEL_NATIVE_HEADER + length);
}
static wl_err_t demo_kernel_route(wl_kernel_t *core, const wl_kernel_frame_t *frame) {
  demo_kernel_t *endpoint = (demo_kernel_t *)core;
  (void)endpoint;
  switch (frame->message_id) {
  case 41U: return wl_kernel_response(core, frame);
  case 40U: {
    if (frame->kind != WL_WIRE_RPC_REQ) return WL_ERR_CORRUPT_PAYLOAD;
    if (endpoint->handlers.on_rpc_execute == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_kernel_rpc_metadata_t metadata;
    wl_err_t error = wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
    if (error != WL_OK) return error;
    uint8_t reply;
    uint8_t *payload;
    error = wl_kernel_reply_claim(core, 41U, &metadata, &reply, &payload, NULL);
    if (error != WL_OK) return error;
    request_t request;
    if (request_decode(metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, reply);
      return WL_ERR_CORRUPT_PAYLOAD;
    }
    response_clear(&endpoint->reply.execute);
    int32_t status = endpoint->handlers.on_rpc_execute(endpoint->user, &request, &endpoint->reply.execute);
    if (status != 0) return wl_kernel_reply_error(core, reply, status);
    size_t length = 0U;
    if (response_encode(&endpoint->reply.execute, payload, sizeof(endpoint->replies[0]) - WL_KERNEL_NATIVE_HEADER - WL_KERNEL_RPC_HEADER_DATA, &length) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, reply);
      return WL_ERR_CORRUPT_PAYLOAD;
    }
    return wl_kernel_reply_commit(core, reply, length);
  }
  case 42U: {
    if (frame->kind != WL_WIRE_MSG) return WL_ERR_CORRUPT_PAYLOAD;
    event_t message;
    if (event_decode(frame->payload, frame->length, &message) != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
    if (endpoint->handlers.on_message_event != NULL) endpoint->handlers.on_message_event(endpoint->user, &message);
    return WL_OK;
  }
  default: return WL_ERR_NOT_SUPPORTED;
  }
}
wl_err_t demo_kernel_init(demo_kernel_t *endpoint, wl_packet_port_t *port, const demo_kernel_handlers_t *handlers, wl_kernel_error_fn error, void *user) {
  if (endpoint == NULL || port == NULL || handlers == NULL) return WL_ERR_INVALID_ARG;
  const demo_kernel_handlers_t saved = *handlers;
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
      .route = demo_kernel_route,
      .on_error = error,
      .user = user,
  };
  return wl_kernel_init(&endpoint->core, &config);
}
wl_err_t demo_kernel_step(demo_kernel_t *endpoint, uint32_t now) {
  return endpoint == NULL ? WL_ERR_INVALID_ARG : wl_kernel_step(&endpoint->core, now, WL_KERNEL_BUDGET);
}
