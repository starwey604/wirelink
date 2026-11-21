/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator.h"
#include <stddef.h>
#include <string.h>

#if ADD_REQUEST_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if ADD_RESPONSE_CODEC_PROFILE != 1U
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

static size_t wlc_scalar_shape_0_measure_fields(const wlc_scalar_shape_0_t *v) {
  (void)v;
  size_t size = 0U;
    size += 1U + wl_codec_vsize((uint64_t)wl_codec_zz32(v->v0));
    size += 1U + wl_codec_vsize((uint64_t)wl_codec_zz32(v->v1));
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
  if (out == NULL || capacity < 12U) {
    const size_t need = wlc_scalar_shape_0_measure_fields(&v);
    if (need != 0U && out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
    if (capacity < need) return WL_CODEC_ERR_CAPACITY;
    if (need == 0U) { *length = 0U; return WL_CODEC_OK; }
  }
  uint8_t *cursor = out;
  *cursor++ = 0x08U;
  cursor = wl_codec_putv(cursor, (uint64_t)wl_codec_zz32(v.v0));
  *cursor++ = 0x10U;
  cursor = wl_codec_putv(cursor, (uint64_t)wl_codec_zz32(v.v1));
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

static wl_codec_status_t wlc_scalar_shape_1_decode(const uint8_t *input, size_t length, void *out) {
  if (out == NULL || (length != 0U && input == NULL)) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_1_t temp;
  wlc_scalar_shape_1_clear(&temp);
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

void add_request_clear(add_request_t *value) {
  _Static_assert(sizeof(add_request_t) == sizeof(wlc_scalar_shape_0_t), "shape layout mismatch");
  wlc_scalar_shape_0_clear(value);
}
size_t add_request_encoded_size(const add_request_t *value) { return wlc_scalar_shape_0_encoded_size(value); }
wl_codec_status_t add_request_encode(const add_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_0_encode(value, out, out_capacity, out_length); }
wl_codec_status_t add_request_decode(const uint8_t *input, size_t input_length, add_request_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  add_request_t temp;
  wl_codec_status_t status = wlc_scalar_shape_0_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}

void add_response_clear(add_response_t *value) {
  _Static_assert(sizeof(add_response_t) == sizeof(wlc_scalar_shape_1_t), "shape layout mismatch");
  wlc_scalar_shape_1_clear(value);
}
size_t add_response_encoded_size(const add_response_t *value) { return wlc_scalar_shape_1_encoded_size(value); }
wl_codec_status_t add_response_encode(const add_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_1_encode(value, out, out_capacity, out_length); }
wl_codec_status_t add_response_decode(const uint8_t *input, size_t input_length, add_response_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  add_response_t temp;
  wl_codec_status_t status = wlc_scalar_shape_1_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}


#include <limits.h>
static void calculator_complete_add(wl_endpoint_t *core, const wl_endpoint_call_t *call, wl_rpc_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  add_response_t response;
  const add_response_t *view = NULL;
  if (result.outcome == WL_RPC_SUCCESS) {
    if (add_response_decode(body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((calculator_add_cb_t)call->callback)((calculator_node_t *)core, result, view);
}
static void calculator_complete_add_ex(wl_endpoint_t *core, const wl_endpoint_call_t *call, wl_rpc_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  add_response_t response;
  const add_response_t *view = NULL;
  if (result.outcome == WL_RPC_SUCCESS) {
    if (add_response_decode(body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((calculator_add_ex_cb_t)call->callback)(call->user, result, view);
}
static wl_err_t calculator_call_add_start(calculator_node_t *self, const add_request_t *request, uint32_t now, uint32_t timeout, uint8_t has_now, wl_endpoint_complete_fn complete, void (*callback)(void), void *user, wl_call_handle_t *out) {
  if (out != NULL) *out = (wl_call_handle_t){0};
  if (self == NULL || request == NULL || callback == NULL || timeout > INT32_MAX) return WL_ERR_INVALID_ARG;
  if (timeout == 0U) timeout = 500U;
  uint8_t slot; uint8_t *payload; size_t capacity;
  wl_err_t error = wl_endpoint_call_claim(&self->core, &slot, &payload, &capacity);
  if (error != WL_OK) return error;
  size_t length = 0U;
  if (add_request_encode(request, payload, capacity, &length) != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  return wl_endpoint_call_commit(&self->core, slot, 20U, 21U, length, now, timeout, has_now, complete, callback, user, out);
}
wl_err_t calculator_call_add(calculator_node_t *self, const add_request_t *request, calculator_add_cb_t callback) {
  return calculator_call_add_start(self, request, 0U, 0U, 0U, calculator_complete_add, (void (*)(void))callback, self, NULL);
}
wl_err_t calculator_call_add_ex(calculator_node_t *self, const add_request_t *request, uint32_t now, uint32_t timeout_ms, calculator_add_ex_cb_t callback, void *user, wl_call_handle_t *out) {
  return calculator_call_add_start(self, request, now, timeout_ms, 1U, calculator_complete_add_ex, (void (*)(void))callback, user, out);
}
static void calculator_error(void *user, uint16_t id, wl_err_t error) {
  calculator_node_t *self = user;
  if (self->on_error != NULL) self->on_error(self->user, id, error);
}
static wl_err_t calculator_route(wl_endpoint_t *core, const wl_wire_frame_t *frame) {
  calculator_node_t *self = (calculator_node_t *)core;
  (void)self;
  switch (frame->message_id) {
  case 21U: return wl_endpoint_response(core, frame);
  default: return WL_ERR_NOT_SUPPORTED;
  }
}
static wl_packet_submit_t calculator_submit(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  (void)token;
  calculator_node_t *self = user;
  wl_err_t error = self->tx_write(self, data, length);
  if (error == WL_OK) return WL_PACKET_SENT;
  return error == WL_ERR_WOULD_BLOCK || error == WL_ERR_BUSY ? WL_PACKET_BUSY : WL_PACKET_FAILED;
}
wl_err_t calculator_init_io(calculator_node_t *self, uint32_t instance, const wl_packet_io_t *io) {
  if (self == NULL || io == NULL || io->submit == NULL) return WL_ERR_INVALID_ARG;
  const wl_packet_io_t binding = *io;
  const int external_rx = binding.rx_storage != NULL || binding.rx_storage_size != 0U || binding.rx_unit_size != 0U;
  if (external_rx && (binding.rx_storage == NULL || binding.rx_unit_size < CALCULATOR_RX_WIRE_MAX + 1U ||
      binding.rx_unit_size > binding.rx_storage_size / WL_PACKET_RX_SLOTS)) return WL_ERR_INVALID_ARG;
  wl_err_t error;
  if (instance == 0U) {
    error = wl_packet_instance_next(&instance);
    if (error != WL_OK) return error;
  }
  memset(self, 0, sizeof(*self));
  wl_packet_config_t packet = {.instance = instance,
      .rx_storage = external_rx ? binding.rx_storage : &self->rx[0][0],
      .rx_unit_size = external_rx ? binding.rx_unit_size : sizeof(self->rx[0]),
      .rx_storage_size = external_rx ? binding.rx_storage_size : sizeof(self->rx),
      .submit = binding.submit, .notify = binding.notify, .user = binding.user};
  error = wl_packet_init(&self->port, &packet);
  if (error != WL_OK) return error;
  wl_endpoint_config_t config = {.port = &self->port, .route = calculator_route, .on_error = calculator_error, .user = self,
      .storage = {
          .requests = &self->requests[0][0], .request_stride = 24U,
          .replies = NULL, .reply_stride = 0U,
          .message = NULL, .message_capacity = 0U,
      }};
  error = wl_endpoint_init(&self->core, &config);
  if (error != WL_OK) wl_packet_stop(&self->port);
  return error;
}
static wl_err_t calculator_init_sync(calculator_node_t *self, uint32_t instance, calculator_tx_write_fn tx_write) {
  if (tx_write == NULL) return WL_ERR_INVALID_ARG;
  const wl_packet_io_t io = {.submit = calculator_submit, .user = self};
  wl_err_t error = calculator_init_io(self, instance, &io);
  if (error == WL_OK) self->tx_write = tx_write;
  return error;
}
wl_err_t calculator_init(calculator_node_t *self, calculator_tx_write_fn tx_write) {
  return calculator_init_sync(self, 0U, tx_write);
}
wl_err_t calculator_init_with_instance(calculator_node_t *self, uint32_t instance, calculator_tx_write_fn tx_write) {
  if (instance == 0U) return WL_ERR_INVALID_ARG;
  return calculator_init_sync(self, instance, tx_write);
}
wl_err_t calculator_rx_packet(calculator_node_t *self, const uint8_t *data, size_t length) {
  if (self == NULL) return WL_ERR_INVALID_ARG;
  if (length > CALCULATOR_RX_WIRE_MAX) return WL_ERR_BUF_TOO_SMALL;
  return wl_packet_rx_push(&self->port, data, length);
}
wl_err_t calculator_step(calculator_node_t *self, uint32_t now) {
  return self == NULL ? WL_ERR_INVALID_ARG : wl_endpoint_step(&self->core, now, WL_ENDPOINT_BUDGET);
}
wl_err_t calculator_close(calculator_node_t *self) {
  if (self == NULL) return WL_ERR_INVALID_ARG;
  wl_err_t error = wl_endpoint_close(&self->core);
  return error;
}
int calculator_quiescent(const calculator_node_t *self) {
  return self != NULL && wl_endpoint_quiescent(&self->core);
}
wl_err_t calculator_cancel(calculator_node_t *self, wl_call_handle_t handle) {
  return self == NULL ? WL_ERR_INVALID_ARG : wl_endpoint_cancel(&self->core, handle);
}
