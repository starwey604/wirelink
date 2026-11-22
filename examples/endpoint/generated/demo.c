/* SPDX-License-Identifier: Apache-2.0 */
#include "demo.h"
#include <stddef.h>
#include <string.h>

#if REQUEST_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if RESPONSE_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if EVENT_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif

typedef struct wlc_scalar_shape_0_s {
  int32_t v0;
} wlc_scalar_shape_0_t;

static void wlc_scalar_shape_0_clear(void *value) {
  if (value == NULL) return;
  wlc_scalar_shape_0_t temp = {0};
  memcpy(value, &temp, sizeof(temp));
}

static size_t wlc_scalar_shape_0_measure_fields(const wlc_scalar_shape_0_t *v) {
  (void)v;
  size_t size = 0U;
    size += 1U + wl_codec_vsize((uint64_t)wl_codec_zz32(v->v0));
  return size;
}

static size_t wlc_scalar_shape_0_encoded_size(const void *value) {
  if (value == NULL) return SIZE_MAX;
  wlc_scalar_shape_0_t v;
  memcpy(&v, value, sizeof(v));
  return wlc_scalar_shape_0_measure_fields(&v);
}

static wl_codec_status_t wlc_scalar_shape_0_encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_0_t v;
  memcpy(&v, value, sizeof(v));
  if (length == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  if (out == NULL || capacity < 6U) {
    const size_t need = wlc_scalar_shape_0_measure_fields(&v);
    if (need != 0U && out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
    if (capacity < need) return WL_CODEC_ERR_CAPACITY;
    if (need == 0U) { *length = 0U; return WL_CODEC_OK; }
  }
  uint8_t *cursor = out;
  *cursor++ = 0x08U;
  cursor = wl_codec_putv(cursor, (uint64_t)wl_codec_zz32(v.v0));
  *length = (size_t)(cursor - out);
  return WL_CODEC_OK;
}

static wl_codec_status_t wlc_scalar_shape_0_decode(const uint8_t *input, size_t length, void *out) {
  if (out == NULL || (length != 0U && input == NULL)) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_0_t temp;
  wlc_scalar_shape_0_clear(&temp);
  uint8_t seen = 0U;
  const uint8_t *cursor = input;
  const uint8_t *end = length != 0U ? input + length : input;
  while (cursor < end) {
    uint64_t wide_key = 0U;
    wl_codec_status_t status = wl_codec_getv(&cursor, end, &wide_key);
    if (status != WL_CODEC_OK) return status;
    const uint64_t number = wide_key >> 3U;
    const uint8_t wire = (uint8_t)(wide_key & 7U);
    if (number == 0U || number > UINT16_MAX || (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    switch ((uint16_t)number) {
      case 1U: {
        if (wire != 0U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 1U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t w = 0U;
        wl_codec_status_t s = wl_codec_getv(&cursor, end, &w);
        if (s != WL_CODEC_OK) return s;
        if (w > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        const int32_t sv = wl_codec_unzz32((uint32_t)w);
        temp.v0 = (int32_t)sv;
        seen |= 1U;
        break;
      }
      default: {
        status = wl_codec_skip(&cursor, end, wire);
        if (status != WL_CODEC_OK) return status;
        break;
      }
    }
  }
  if ((seen & 1U) != 1U) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  memcpy(out, &temp, sizeof(temp));
  return WL_CODEC_OK;
}

void request_clear(request_t *value) {
  _Static_assert(sizeof(request_t) == sizeof(wlc_scalar_shape_0_t), "shape layout mismatch");
  wlc_scalar_shape_0_clear(value);
}
size_t request_encoded_size(const request_t *value) { return wlc_scalar_shape_0_encoded_size(value); }
wl_codec_status_t request_encode(const request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_0_encode(value, out, out_capacity, out_length); }
wl_codec_status_t request_decode(const uint8_t *input, size_t input_length, request_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  request_t temp;
  wl_codec_status_t status = wlc_scalar_shape_0_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}

void response_clear(response_t *value) {
  _Static_assert(sizeof(response_t) == sizeof(wlc_scalar_shape_0_t), "shape layout mismatch");
  wlc_scalar_shape_0_clear(value);
}
size_t response_encoded_size(const response_t *value) { return wlc_scalar_shape_0_encoded_size(value); }
wl_codec_status_t response_encode(const response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_0_encode(value, out, out_capacity, out_length); }
wl_codec_status_t response_decode(const uint8_t *input, size_t input_length, response_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  response_t temp;
  wl_codec_status_t status = wlc_scalar_shape_0_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}

static const wl_codec_field_t event_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_INLINE_BYTES, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(event_t, data), .count_offset = (uint16_t)(offsetof(event_t, data.data) - offsetof(event_t, data)), .capacity_offset = 0U, .max_length = 9U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
};
static const wl_codec_desc_t event_desc = {
  .fields = event_fields,
  .field_count = 1U,
  .struct_size = sizeof(event_t),
  .presence_bytes = 0U,
  .min_number = 1U,
  .max_number = 1U,
  .lookup = WL_CODEC_LOOKUP_LINEAR,
  .has_repeated = 0U,
  .dense_map = NULL,
  .clear = NULL,
  .measure = NULL,
  .encode = NULL,
  .decode = NULL
};

void event_clear(event_t *value) { wl_codec_clear(&event_desc, value); }
size_t event_encoded_size(const event_t *value) { size_t size = 0U; return wl_codec_measure(&event_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t event_encode(const event_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&event_desc, value, out, out_capacity, out_length); }
wl_codec_status_t event_decode(const uint8_t *input, size_t input_length, event_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; event_t scratch; return wl_codec_decode(&event_desc, input, input_length, out, &scratch); }


#include <limits.h>
wl_err_t demo_send_event(demo_node_t *self, const event_t *message) {
  if (self == NULL || message == NULL) return WL_ERR_INVALID_ARG;
  uint8_t *frame; size_t capacity;
  wl_err_t error = wl_endpoint_msg_claim(&self->node.core, &frame, &capacity);
  if (error != WL_OK) return error;
  size_t length = 0U;
  if (event_encode(message, frame + 4U, capacity - 4U, &length) != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  error = wl_wire_frame_seal(frame, capacity, WL_WIRE_MSG, 42U, length);
  return error == WL_OK ? wl_endpoint_msg_commit(&self->node.core, length + 4U) : error;
}
static void demo_complete_execute(wl_endpoint_t *core, const wl_endpoint_call_t *call, wl_rpc_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  response_t response;
  const response_t *view = NULL;
  if (result.outcome == WL_RPC_SUCCESS) {
    if (response_decode(body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((demo_execute_cb_t)call->callback)((demo_node_t *)core, result, view);
}
static void demo_complete_execute_ex(wl_endpoint_t *core, const wl_endpoint_call_t *call, wl_rpc_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  response_t response;
  const response_t *view = NULL;
  if (result.outcome == WL_RPC_SUCCESS) {
    if (response_decode(body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((demo_execute_ex_cb_t)call->callback)(call->user, result, view);
}
static wl_err_t demo_call_execute_start(demo_node_t *self, const request_t *request, uint32_t now, uint32_t timeout, uint8_t has_now, wl_endpoint_complete_fn complete, void (*callback)(void), void *user, wl_call_handle_t *out) {
  if (out != NULL) *out = (wl_call_handle_t){0};
  if (self == NULL || request == NULL || callback == NULL || timeout > INT32_MAX) return WL_ERR_INVALID_ARG;
  if (timeout == 0U) timeout = 500U;
  uint8_t slot; uint8_t *payload; size_t capacity;
  wl_err_t error = wl_endpoint_call_claim(&self->node.core, &slot, &payload, &capacity);
  if (error != WL_OK) return error;
  size_t length = 0U;
  if (request_encode(request, payload, capacity, &length) != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  return wl_endpoint_call_commit(&self->node.core, slot, 40U, 41U, length, now, timeout, has_now, complete, callback, user, out);
}
wl_err_t demo_call_execute(demo_node_t *self, const request_t *request, demo_execute_cb_t callback) {
  return demo_call_execute_start(self, request, 0U, 0U, 0U, demo_complete_execute, (void (*)(void))callback, self, NULL);
}
wl_err_t demo_call_execute_ex(demo_node_t *self, const request_t *request, uint32_t now, uint32_t timeout_ms, demo_execute_ex_cb_t callback, void *user, wl_call_handle_t *out) {
  return demo_call_execute_start(self, request, now, timeout_ms, 1U, demo_complete_execute_ex, (void (*)(void))callback, user, out);
}
static wl_err_t demo_route(wl_endpoint_t *core, const wl_wire_frame_t *frame) {
  demo_node_t *self = (demo_node_t *)core;
  (void)self;
  switch (frame->message_id) {
  case 41U: return wl_endpoint_response(core, frame);
  case 40U: {
    if (frame->kind != WL_WIRE_RPC_REQ) return WL_ERR_CORRUPT_PAYLOAD;
    if (self->handlers.on_execute == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_rpc_metadata_t metadata;
    wl_err_t error = wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
    if (error != WL_OK) return error;
    uint8_t slot; uint8_t *payload; size_t capacity;
    error = wl_endpoint_reply_claim(core, 41U, &metadata, &slot, &payload, &capacity);
    if (error != WL_OK) return error;
    request_t request;
    if (request_decode(metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_endpoint_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    response_clear(&self->reply.execute);
    int32_t status = self->handlers.on_execute(self->node.user, &request, &self->reply.execute);
    if (status != 0) return wl_endpoint_reply_error(core, slot, status);
    size_t length = 0U;
    if (response_encode(&self->reply.execute, payload, capacity, &length) != WL_CODEC_OK) {
      wl_endpoint_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    return wl_endpoint_reply_commit(core, slot, length);
  }
  case 42U: {
    if (frame->kind != WL_WIRE_MSG) return WL_ERR_CORRUPT_PAYLOAD;
    event_t message;
    if (event_decode(frame->payload, frame->length, &message) != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
    if (self->handlers.on_event != NULL) self->handlers.on_event(self->node.user, &message);
    return WL_OK;
  }
  default: return WL_ERR_NOT_SUPPORTED;
  }
}
static wl_err_t demo_sync_write(wl_node_t *node, const uint8_t *data, size_t length) {
  demo_node_t *self = (demo_node_t *)node;
  return self->tx_write(self, data, length);
}
static wl_err_t demo_bind_impl(demo_node_t *self, uint32_t instance, wl_transport_t *transport, demo_tx_write_fn tx_write, const demo_handlers_t *handlers, void *user) {
  if (self == NULL) return WL_ERR_INVALID_ARG;
  if (transport == NULL && tx_write == NULL) return WL_ERR_INVALID_ARG;
  if (handlers == NULL) return WL_ERR_INVALID_ARG;
  self->handlers = *handlers;
  self->tx_write = tx_write;
  const wl_node_layout_t layout = {
    .instance = instance, .has_tx = 1,
    .route = demo_route, .user = user,
    .rx_storage = &self->rx[0][0], .rx_storage_size = sizeof(self->rx),
    .rx_frame_max = DEMO_RX_FRAME_MAX, .tx_frame_max = DEMO_TX_FRAME_MAX,
    .rx_wire_max = DEMO_RX_WIRE_MAX, .tx_wire_max = DEMO_TX_WIRE_MAX,
    .storage = {
          .requests = &self->requests[0][0], .request_stride = 18U,
          .replies = &self->replies[0][0], .reply_stride = 18U,
          .message = self->message, .message_capacity = 15U,
    }
  };
  return transport != NULL ? wl_node_bind_transport(&self->node, &layout, transport)
                           : wl_node_bind_sync(&self->node, &layout, demo_sync_write);
}
wl_err_t demo_bind(demo_node_t *self, wl_transport_t *transport, const demo_handlers_t *handlers, void *user) {
  if (transport == NULL) return WL_ERR_INVALID_ARG;
  return demo_bind_impl(self, 0U, transport, NULL, handlers, user);
}
wl_err_t demo_bind_instance(demo_node_t *self, uint32_t instance, wl_transport_t *transport, const demo_handlers_t *handlers, void *user) {
  if (instance == 0U) return WL_ERR_INVALID_ARG;
  if (transport == NULL) return WL_ERR_INVALID_ARG;
  return demo_bind_impl(self, instance, transport, NULL, handlers, user);
}
wl_err_t demo_bind_sync(demo_node_t *self, demo_tx_write_fn tx_write, const demo_handlers_t *handlers, void *user) {
  return demo_bind_impl(self, 0U, NULL, tx_write, handlers, user);
}
wl_err_t demo_bind_sync_instance(demo_node_t *self, uint32_t instance, demo_tx_write_fn tx_write, const demo_handlers_t *handlers, void *user) {
  if (instance == 0U) return WL_ERR_INVALID_ARG;
  return demo_bind_impl(self, instance, NULL, tx_write, handlers, user);
}
