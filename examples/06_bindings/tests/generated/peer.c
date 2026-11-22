/* SPDX-License-Identifier: Apache-2.0 */
#include "peer.h"
#include <stddef.h>
#include <string.h>

#if PEER_ADD_REQUEST_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if PEER_ADD_RESPONSE_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif

typedef struct wlc_scalar_shape_0_s {
  int32_t v0;
  int32_t v1;
} wlc_scalar_shape_0_t;

static void wlc_scalar_shape_0_clear(void *value) {
  if (value == NULL) return;
  wlc_scalar_shape_0_t temp = {0};
  memcpy(value, &temp, sizeof(temp));
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
      case 2U: {
        if (wire != 0U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 2U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t w = 0U;
        wl_codec_status_t s = wl_codec_getv(&cursor, end, &w);
        if (s != WL_CODEC_OK) return s;
        if (w > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        const int32_t sv = wl_codec_unzz32((uint32_t)w);
        temp.v1 = (int32_t)sv;
        seen |= 2U;
        break;
      }
      default: {
        status = wl_codec_skip(&cursor, end, wire);
        if (status != WL_CODEC_OK) return status;
        break;
      }
    }
  }
  if ((seen & 3U) != 3U) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  memcpy(out, &temp, sizeof(temp));
  return WL_CODEC_OK;
}

typedef struct wlc_scalar_shape_1_s {
  int32_t v0;
} wlc_scalar_shape_1_t;

static void wlc_scalar_shape_1_clear(void *value) {
  if (value == NULL) return;
  wlc_scalar_shape_1_t temp = {0};
  memcpy(value, &temp, sizeof(temp));
}

static size_t wlc_scalar_shape_1_measure_fields(const wlc_scalar_shape_1_t *v) {
  (void)v;
  size_t size = 0U;
    size += 1U + wl_codec_vsize((uint64_t)wl_codec_zz32(v->v0));
  return size;
}

static size_t wlc_scalar_shape_1_encoded_size(const void *value) {
  if (value == NULL) return SIZE_MAX;
  wlc_scalar_shape_1_t v;
  memcpy(&v, value, sizeof(v));
  return wlc_scalar_shape_1_measure_fields(&v);
}

static wl_codec_status_t wlc_scalar_shape_1_encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_1_t v;
  memcpy(&v, value, sizeof(v));
  if (length == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  if (out == NULL || capacity < 6U) {
    const size_t need = wlc_scalar_shape_1_measure_fields(&v);
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

void peer_add_request_clear(peer_add_request_t *value) {
  _Static_assert(sizeof(peer_add_request_t) == sizeof(wlc_scalar_shape_0_t), "shape layout mismatch");
  wlc_scalar_shape_0_clear(value);
}
wl_codec_status_t peer_add_request_decode(const uint8_t *input, size_t input_length, peer_add_request_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  peer_add_request_t temp;
  wl_codec_status_t status = wlc_scalar_shape_0_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}

void peer_add_response_clear(peer_add_response_t *value) {
  _Static_assert(sizeof(peer_add_response_t) == sizeof(wlc_scalar_shape_1_t), "shape layout mismatch");
  wlc_scalar_shape_1_clear(value);
}
size_t peer_add_response_encoded_size(const peer_add_response_t *value) { return wlc_scalar_shape_1_encoded_size(value); }
wl_codec_status_t peer_add_response_encode(const peer_add_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_1_encode(value, out, out_capacity, out_length); }


#include <limits.h>
static wl_err_t calculator_peer_route(wl_endpoint_t *core, const wl_wire_frame_t *frame) {
  calculator_peer_node_t *self = (calculator_peer_node_t *)core;
  (void)self;
  switch (frame->message_id) {
  case 20U: {
    if (frame->kind != WL_WIRE_RPC_REQ) return WL_ERR_CORRUPT_PAYLOAD;
    if (self->handlers.on_add == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_rpc_metadata_t metadata;
    wl_err_t error = wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
    if (error != WL_OK) return error;
    uint8_t slot; uint8_t *payload; size_t capacity;
    error = wl_endpoint_reply_claim(core, 21U, &metadata, &slot, &payload, &capacity);
    if (error != WL_OK) return error;
    peer_add_request_t request;
    if (peer_add_request_decode(metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_endpoint_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    peer_add_response_clear(&self->reply.add);
    int32_t status = self->handlers.on_add(self->node.user, &request, &self->reply.add);
    if (status != 0) return wl_endpoint_reply_error(core, slot, status);
    size_t length = 0U;
    if (peer_add_response_encode(&self->reply.add, payload, capacity, &length) != WL_CODEC_OK) {
      wl_endpoint_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    return wl_endpoint_reply_commit(core, slot, length);
  }
  default: return WL_ERR_NOT_SUPPORTED;
  }
}
static wl_err_t calculator_peer_sync_write(wl_node_t *node, const uint8_t *data, size_t length) {
  calculator_peer_node_t *self = (calculator_peer_node_t *)node;
  return self->tx_write(self, data, length);
}
static wl_err_t calculator_peer_bind_impl(calculator_peer_node_t *self, uint32_t instance, wl_transport_t *transport, calculator_peer_tx_write_fn tx_write, const calculator_peer_handlers_t *handlers, void *user) {
  if (self == NULL) return WL_ERR_INVALID_ARG;
  if (transport == NULL && tx_write == NULL) return WL_ERR_INVALID_ARG;
  if (handlers == NULL) return WL_ERR_INVALID_ARG;
  self->handlers = *handlers;
  self->tx_write = tx_write;
  const wl_node_layout_t layout = {
    .instance = instance, .has_tx = 1,
    .route = calculator_peer_route, .user = user,
    .rx_storage = &self->rx[0][0], .rx_storage_size = sizeof(self->rx),
    .rx_frame_max = CALCULATOR_PEER_RX_FRAME_MAX, .tx_frame_max = CALCULATOR_PEER_TX_FRAME_MAX,
    .rx_wire_max = CALCULATOR_PEER_RX_WIRE_MAX, .tx_wire_max = CALCULATOR_PEER_TX_WIRE_MAX,
    .storage = {
          .requests = NULL, .request_stride = 0U,
          .replies = &self->replies[0][0], .reply_stride = 18U,
          .message = NULL, .message_capacity = 0U,
    }
  };
  return transport != NULL ? wl_node_bind_transport(&self->node, &layout, transport)
                           : wl_node_bind_sync(&self->node, &layout, calculator_peer_sync_write);
}
wl_err_t calculator_peer_bind(calculator_peer_node_t *self, wl_transport_t *transport, const calculator_peer_handlers_t *handlers, void *user) {
  if (transport == NULL) return WL_ERR_INVALID_ARG;
  return calculator_peer_bind_impl(self, 0U, transport, NULL, handlers, user);
}
wl_err_t calculator_peer_bind_instance(calculator_peer_node_t *self, uint32_t instance, wl_transport_t *transport, const calculator_peer_handlers_t *handlers, void *user) {
  if (instance == 0U) return WL_ERR_INVALID_ARG;
  if (transport == NULL) return WL_ERR_INVALID_ARG;
  return calculator_peer_bind_impl(self, instance, transport, NULL, handlers, user);
}
wl_err_t calculator_peer_bind_sync(calculator_peer_node_t *self, calculator_peer_tx_write_fn tx_write, const calculator_peer_handlers_t *handlers, void *user) {
  return calculator_peer_bind_impl(self, 0U, NULL, tx_write, handlers, user);
}
wl_err_t calculator_peer_bind_sync_instance(calculator_peer_node_t *self, uint32_t instance, calculator_peer_tx_write_fn tx_write, const calculator_peer_handlers_t *handlers, void *user) {
  if (instance == 0U) return WL_ERR_INVALID_ARG;
  return calculator_peer_bind_impl(self, instance, NULL, tx_write, handlers, user);
}
