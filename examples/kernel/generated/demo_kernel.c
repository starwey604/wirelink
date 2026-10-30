/* SPDX-License-Identifier: Apache-2.0 */
#include "demo_kernel.h"
#include "demo_kernel_codecs.h"
#include <string.h>
static wl_err_t demo_kernel_encode_request(const void *value, uint8_t *data, size_t capacity, size_t *length) {
  return demo_rpc_request_value_encode(value, data, capacity, length) == WL_CODEC_OK ? WL_OK : WL_ERR_CORRUPT_PAYLOAD;
}
static wl_err_t demo_kernel_encode_response(const void *value, uint8_t *data, size_t capacity, size_t *length) {
  return demo_rpc_response_value_encode(value, data, capacity, length) == WL_CODEC_OK ? WL_OK : WL_ERR_CORRUPT_PAYLOAD;
}
static void demo_kernel_complete_execute(wl_kernel_t *core, const wl_kernel_call_t *call, wl_kernel_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  response_t response;
  const response_t *view = NULL;
  if (result.outcome == WL_KERNEL_SUCCESS) {
    if (demo_rpc_response_decode(body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((demo_kernel_execute_callback_t)call->callback)(call->user, result, view);
}
wl_err_t demo_kernel_execute_start(demo_kernel_t *endpoint, const request_value_t *request, uint32_t now, uint32_t timeout, demo_kernel_execute_callback_t callback, void *user, wl_kernel_call_handle_t *out) {
  if (endpoint == NULL || request == NULL) return WL_ERR_INVALID_ARG;
  return wl_kernel_call_start(&endpoint->core, 40U, 41U, request, demo_kernel_encode_request, now, timeout, demo_kernel_complete_execute, (void (*)(void))callback, user, out);
}
static wl_err_t demo_kernel_encode_event(const void *value, uint8_t *data, size_t capacity, size_t *length) {
  return event_value_encode(value, data, capacity, length) == WL_CODEC_OK ? WL_OK : WL_ERR_CORRUPT_PAYLOAD;
}
wl_err_t demo_kernel_send_event(demo_kernel_t *endpoint, const event_value_t *value) {
  if (endpoint == NULL || value == NULL) return WL_ERR_INVALID_ARG;
  return wl_kernel_send(&endpoint->core, 42U, value, demo_kernel_encode_event);
}
static wl_err_t demo_kernel_encode_event_view(const void *value, uint8_t *data, size_t capacity, size_t *length) {
  return event_encode(value, data, capacity, length) == WL_CODEC_OK ? WL_OK : WL_ERR_CORRUPT_PAYLOAD;
}
wl_err_t demo_kernel_send_event_view(demo_kernel_t *endpoint, const event_t *value) {
  if (endpoint == NULL || value == NULL) return WL_ERR_INVALID_ARG;
  return wl_kernel_send(&endpoint->core, 42U, value, demo_kernel_encode_event_view);
}
static wl_err_t demo_kernel_route(wl_kernel_t *core, const wl_kernel_frame_t *frame) {
  demo_kernel_t *endpoint = (demo_kernel_t *)core;
  (void)endpoint;
  switch (frame->message_id) {
  case 41U: return wl_kernel_response(core, frame->message_id, frame->payload, frame->length);
  case 40U: {
    if (endpoint->handlers.on_rpc_execute == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_kernel_rpc_metadata_t metadata;
    wl_err_t error = wl_kernel_rpc_read(frame->payload, frame->length, 1, &metadata);
    if (error != WL_OK) return error;
    unsigned reply;
    error = wl_kernel_reply_reserve(core, &reply);
    if (error != WL_OK) return error;
    request_t request;
    if (demo_rpc_request_decode(metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, reply);
      return WL_ERR_CORRUPT_PAYLOAD;
    }
    demo_rpc_response_value_clear(&endpoint->reply.execute);
    int32_t status = endpoint->handlers.on_rpc_execute(endpoint->user, &request, &endpoint->reply.execute);
    return wl_kernel_reply_finish(core, reply, 41U, &metadata, status, &endpoint->reply.execute, demo_kernel_encode_response);
  }
  case 42U: {
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
  wl_kernel_config_t config = {.port = port, .route = demo_kernel_route, .on_error = error, .user = user};
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i) config.tx_storage[i] = (wl_span_t){endpoint->requests[i], sizeof(endpoint->requests[i])};
  for (unsigned i = 0U; i < WL_KERNEL_REPLIES; ++i) config.tx_storage[i + WL_KERNEL_CALLS] = (wl_span_t){endpoint->replies[i], sizeof(endpoint->replies[i])};
  config.tx_storage[WL_KERNEL_TX_COUNT - 1U] = (wl_span_t){endpoint->message, sizeof(endpoint->message)};
  return wl_kernel_init(&endpoint->core, &config);
}
wl_err_t demo_kernel_step(demo_kernel_t *endpoint, uint32_t now) {
  return endpoint == NULL ? WL_ERR_INVALID_ARG : wl_kernel_step(&endpoint->core, now, WL_KERNEL_BUDGET);
}
