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

static const wl_codec_desc_t event_desc;

typedef struct wlc_scalar_shape_0_s {
  bool h0;
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
  if (!v.h0) return SIZE_MAX;
  return wlc_scalar_shape_0_measure_fields(&v);
}

static wl_codec_status_t wlc_scalar_shape_0_encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_0_t v;
  memcpy(&v, value, sizeof(v));
  if (!v.h0) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
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
        if (temp.h0) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t w = 0U;
        wl_codec_status_t s = wl_codec_getv(&cursor, end, &w);
        if (s != WL_CODEC_OK) return s;
        if (w > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        const int32_t sv = wl_codec_unzz32((uint32_t)w);
        temp.v0 = (int32_t)sv;
        temp.h0 = true;
        break;
      }
      default: {
        status = wl_codec_skip(&cursor, end, wire);
        if (status != WL_CODEC_OK) return status;
        break;
      }
    }
  }
  if (!temp.h0) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
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

void event_clear(event_t *value) { wl_codec_clear(&event_desc, value); }
size_t event_encoded_size(const event_t *value) { size_t size = 0U; return wl_codec_measure(&event_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t event_encode(const event_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&event_desc, value, out, out_capacity, out_length); }
wl_codec_status_t event_decode(const uint8_t *input, size_t input_length, event_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; event_t scratch; return wl_codec_decode(&event_desc, input, input_length, out, &scratch); }

static const wl_codec_field_t event_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_INLINE_BYTES, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(event_t, has_data), .offset = (uint16_t)offsetof(event_t, data), .count_offset = (uint16_t)(offsetof(event_t, data.data) - offsetof(event_t, data)), .capacity_offset = 0U, .max_length = 9U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
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
