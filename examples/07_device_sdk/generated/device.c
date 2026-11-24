/* SPDX-License-Identifier: Apache-2.0 */
#include "device.h"
#include <stddef.h>
#include <string.h>

#if INFO_REQUEST_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if SETTINGS_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if INFO_RESPONSE_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if CONFIGURE_REQUEST_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if CONFIGURE_RESPONSE_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if NUMBERS_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif

static const wl_codec_desc_t info_request_desc = {
  .fields = NULL,
  .field_count = 0U,
  .struct_size = sizeof(info_request_t),
  .presence_bytes = 0U,
  .min_number = 0U,
  .max_number = 0U,
  .lookup = WL_CODEC_LOOKUP_LINEAR,
  .has_repeated = 0U,
  .dense_map = NULL,
  .clear = NULL,
  .measure = NULL,
  .encode = NULL,
  .decode = NULL
};

void info_request_clear(info_request_t *value) { wl_codec_clear(&info_request_desc, value); }
size_t info_request_encoded_size(const info_request_t *value) { size_t size = 0U; return wl_codec_measure(&info_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t info_request_encode(const info_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&info_request_desc, value, out, out_capacity, out_length); }
wl_codec_status_t info_request_decode(const uint8_t *input, size_t input_length, info_request_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; info_request_t scratch; return wl_codec_decode(&info_request_desc, input, input_length, out, &scratch); }

static void settings_wlc_clear_v(void *value);
static wl_codec_status_t settings_wlc_measure_v(const void *value, size_t *out_size);
static wl_codec_status_t settings_wlc_encode_v(const void *value, uint8_t *out, size_t capacity, size_t *length);
static wl_codec_status_t settings_wlc_decode_v(const uint8_t *input, size_t length, void *out);
static const char settings_label_default_str[] = "\xE8""\xAE""\xBE""\xE5""\xA4""\x87";
static const wl_codec_field_t settings_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_INLINE_STRING, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(settings_t, has_label), .offset = (uint16_t)offsetof(settings_t, label), .count_offset = (uint16_t)(offsetof(settings_t, label.data) - offsetof(settings_t, label)), .capacity_offset = 0U, .max_length = 12U, .element_stride = 0U, .default_bits = UINT64_C(6), .string_default = settings_label_default_str, .child = NULL },
  { .number = 2U, .kind = WL_CODEC_KIND_BOOL, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 1U, .is_signed = 0U, .key_len = 1U, .key = { 0x10U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(settings_t, enabled), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 3U, .kind = WL_CODEC_KIND_ENUM, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 4U, .is_signed = 1U, .key_len = 1U, .key = { 0x18U, 0x00U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(settings_t, has_mode), .offset = (uint16_t)offsetof(settings_t, mode), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(1), .string_default = NULL, .child = NULL },
  { .number = 4U, .kind = WL_CODEC_KIND_INLINE_BYTES, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x22U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(settings_t, token), .count_offset = (uint16_t)(offsetof(settings_t, token.data) - offsetof(settings_t, token)), .capacity_offset = 0U, .max_length = 8U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 5U, .kind = WL_CODEC_KIND_FLOAT32, .cardinality = WL_CODEC_CARD_REQUIRED_PACKED, .wire_type = 2U, .scalar_size = 4U, .is_signed = 0U, .key_len = 1U, .key = { 0x2aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(settings_t, gains), .count_offset = 0U, .capacity_offset = 0U, .max_length = 3U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 6U, .kind = WL_CODEC_KIND_FIXED64, .cardinality = WL_CODEC_CARD_PACKED, .wire_type = 2U, .scalar_size = 8U, .is_signed = 0U, .key_len = 1U, .key = { 0x32U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(settings_t, has_counters), .offset = (uint16_t)offsetof(settings_t, counters), .count_offset = 0U, .capacity_offset = 0U, .max_length = 2U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 7U, .kind = WL_CODEC_KIND_INT64, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 8U, .is_signed = 1U, .key_len = 1U, .key = { 0x38U, 0x00U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(settings_t, has_floor), .offset = (uint16_t)offsetof(settings_t, floor), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(9223372036854775808), .string_default = NULL, .child = NULL },
  { .number = 8U, .kind = WL_CODEC_KIND_UINT64, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 8U, .is_signed = 0U, .key_len = 1U, .key = { 0x40U, 0x00U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(settings_t, has_ceiling), .offset = (uint16_t)offsetof(settings_t, ceiling), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(18446744073709551615), .string_default = NULL, .child = NULL },
  { .number = 9U, .kind = WL_CODEC_KIND_INLINE_STRING, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x4aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(settings_t, has_class_), .offset = (uint16_t)offsetof(settings_t, class_), .count_offset = (uint16_t)(offsetof(settings_t, class_.data) - offsetof(settings_t, class_)), .capacity_offset = 0U, .max_length = 8U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 10U, .kind = WL_CODEC_KIND_UINT32, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 2U, .is_signed = 0U, .key_len = 1U, .key = { 0x50U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(settings_t, has_timeout), .offset = (uint16_t)offsetof(settings_t, timeout), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
};
static const uint16_t settings_dense[10] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U };
static const wl_codec_desc_t settings_desc = {
  .fields = settings_fields,
  .field_count = 10U,
  .struct_size = sizeof(settings_t),
  .presence_bytes = 0U,
  .min_number = 1U,
  .max_number = 10U,
  .lookup = WL_CODEC_LOOKUP_DENSE,
  .has_repeated = 0U,
  .dense_map = settings_dense,
  .clear = settings_wlc_clear_v,
  .measure = settings_wlc_measure_v,
  .encode = settings_wlc_encode_v,
  .decode = settings_wlc_decode_v
};

static void settings_wlc_clear_v(void *value) { wl_codec_clear(&settings_desc, value); }
static wl_codec_status_t settings_wlc_measure_v(const void *value, size_t *out_size) { return wl_codec_measure(&settings_desc, value, out_size); }
static wl_codec_status_t settings_wlc_encode_v(const void *value, uint8_t *out, size_t capacity, size_t *length) { return wl_codec_encode(&settings_desc, value, out, capacity, length); }
static wl_codec_status_t settings_wlc_decode_v(const uint8_t *input, size_t length, void *out) { settings_t scratch; return wl_codec_decode(&settings_desc, input, length, out, &scratch); }

void settings_clear(settings_t *value) { wl_codec_clear(&settings_desc, value); }
size_t settings_encoded_size(const settings_t *value) { size_t size = 0U; return wl_codec_measure(&settings_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t settings_encode(const settings_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&settings_desc, value, out, out_capacity, out_length); }
wl_codec_status_t settings_decode(const uint8_t *input, size_t input_length, settings_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; settings_t scratch; return wl_codec_decode(&settings_desc, input, input_length, out, &scratch); }

static const wl_codec_field_t info_response_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_INLINE_STRING, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(info_response_t, name), .count_offset = (uint16_t)(offsetof(info_response_t, name.data) - offsetof(info_response_t, name)), .capacity_offset = 0U, .max_length = 32U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 2U, .kind = WL_CODEC_KIND_MESSAGE, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x12U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(info_response_t, settings), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = &settings_desc },
};
static const wl_codec_desc_t info_response_desc = {
  .fields = info_response_fields,
  .field_count = 2U,
  .struct_size = sizeof(info_response_t),
  .presence_bytes = 0U,
  .min_number = 1U,
  .max_number = 2U,
  .lookup = WL_CODEC_LOOKUP_LINEAR,
  .has_repeated = 0U,
  .dense_map = NULL,
  .clear = NULL,
  .measure = NULL,
  .encode = NULL,
  .decode = NULL
};

void info_response_clear(info_response_t *value) { wl_codec_clear(&info_response_desc, value); }
size_t info_response_encoded_size(const info_response_t *value) { size_t size = 0U; return wl_codec_measure(&info_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t info_response_encode(const info_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&info_response_desc, value, out, out_capacity, out_length); }
wl_codec_status_t info_response_decode(const uint8_t *input, size_t input_length, info_response_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; info_response_t scratch; return wl_codec_decode(&info_response_desc, input, input_length, out, &scratch); }

static const wl_codec_field_t configure_request_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_MESSAGE, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(configure_request_t, settings), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = &settings_desc },
  { .number = 2U, .kind = WL_CODEC_KIND_INLINE_BYTES, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x12U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(configure_request_t, has_opaque), .offset = (uint16_t)offsetof(configure_request_t, opaque), .count_offset = (uint16_t)(offsetof(configure_request_t, opaque.data) - offsetof(configure_request_t, opaque)), .capacity_offset = 0U, .max_length = 4U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 3U, .kind = WL_CODEC_KIND_MESSAGE, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x1aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(configure_request_t, has_backup), .offset = (uint16_t)offsetof(configure_request_t, backup), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = &settings_desc },
};
static const wl_codec_desc_t configure_request_desc = {
  .fields = configure_request_fields,
  .field_count = 3U,
  .struct_size = sizeof(configure_request_t),
  .presence_bytes = 0U,
  .min_number = 1U,
  .max_number = 3U,
  .lookup = WL_CODEC_LOOKUP_LINEAR,
  .has_repeated = 0U,
  .dense_map = NULL,
  .clear = NULL,
  .measure = NULL,
  .encode = NULL,
  .decode = NULL
};

void configure_request_clear(configure_request_t *value) { wl_codec_clear(&configure_request_desc, value); }
size_t configure_request_encoded_size(const configure_request_t *value) { size_t size = 0U; return wl_codec_measure(&configure_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t configure_request_encode(const configure_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&configure_request_desc, value, out, out_capacity, out_length); }
wl_codec_status_t configure_request_decode(const uint8_t *input, size_t input_length, configure_request_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; configure_request_t scratch; return wl_codec_decode(&configure_request_desc, input, input_length, out, &scratch); }

static const wl_codec_field_t configure_response_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_MESSAGE, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(configure_response_t, settings), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = &settings_desc },
  { .number = 2U, .kind = WL_CODEC_KIND_INLINE_BYTES, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x12U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(configure_response_t, has_opaque), .offset = (uint16_t)offsetof(configure_response_t, opaque), .count_offset = (uint16_t)(offsetof(configure_response_t, opaque.data) - offsetof(configure_response_t, opaque)), .capacity_offset = 0U, .max_length = 4U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 3U, .kind = WL_CODEC_KIND_MESSAGE, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x1aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(configure_response_t, has_backup), .offset = (uint16_t)offsetof(configure_response_t, backup), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = &settings_desc },
  { .number = 4U, .kind = WL_CODEC_KIND_UINT32, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 4U, .is_signed = 0U, .key_len = 1U, .key = { 0x20U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(configure_response_t, count), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
};
static const wl_codec_desc_t configure_response_desc = {
  .fields = configure_response_fields,
  .field_count = 4U,
  .struct_size = sizeof(configure_response_t),
  .presence_bytes = 0U,
  .min_number = 1U,
  .max_number = 4U,
  .lookup = WL_CODEC_LOOKUP_LINEAR,
  .has_repeated = 0U,
  .dense_map = NULL,
  .clear = NULL,
  .measure = NULL,
  .encode = NULL,
  .decode = NULL
};

void configure_response_clear(configure_response_t *value) { wl_codec_clear(&configure_response_desc, value); }
size_t configure_response_encoded_size(const configure_response_t *value) { size_t size = 0U; return wl_codec_measure(&configure_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t configure_response_encode(const configure_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&configure_response_desc, value, out, out_capacity, out_length); }
wl_codec_status_t configure_response_decode(const uint8_t *input, size_t input_length, configure_response_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; configure_response_t scratch; return wl_codec_decode(&configure_response_desc, input, input_length, out, &scratch); }

static const char numbers_text_default_str[] = "\x61""\x22""\x40""\x4E""\x41""\x4D""\x45""\x40";
static const wl_codec_field_t numbers_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_INT32, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 1U, .is_signed = 1U, .key_len = 1U, .key = { 0x08U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, i8), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 2U, .kind = WL_CODEC_KIND_UINT32, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 1U, .is_signed = 0U, .key_len = 1U, .key = { 0x10U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, u8), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 3U, .kind = WL_CODEC_KIND_INT32, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 2U, .is_signed = 1U, .key_len = 1U, .key = { 0x18U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, i16), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 4U, .kind = WL_CODEC_KIND_UINT32, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 2U, .is_signed = 0U, .key_len = 1U, .key = { 0x20U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, u16), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 5U, .kind = WL_CODEC_KIND_INT32, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 4U, .is_signed = 1U, .key_len = 1U, .key = { 0x28U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, i32), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 6U, .kind = WL_CODEC_KIND_UINT32, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 4U, .is_signed = 0U, .key_len = 1U, .key = { 0x30U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, u32), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 7U, .kind = WL_CODEC_KIND_INT64, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 8U, .is_signed = 1U, .key_len = 1U, .key = { 0x38U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, i64), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 8U, .kind = WL_CODEC_KIND_UINT64, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 8U, .is_signed = 0U, .key_len = 1U, .key = { 0x40U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, u64), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 9U, .kind = WL_CODEC_KIND_FIXED32, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 5U, .scalar_size = 4U, .is_signed = 0U, .key_len = 1U, .key = { 0x4dU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, f32), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 10U, .kind = WL_CODEC_KIND_FIXED64, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 1U, .scalar_size = 8U, .is_signed = 0U, .key_len = 1U, .key = { 0x51U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, f64), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 11U, .kind = WL_CODEC_KIND_FLOAT32, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 5U, .scalar_size = 4U, .is_signed = 0U, .key_len = 1U, .key = { 0x5dU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, single), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 12U, .kind = WL_CODEC_KIND_FLOAT64, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 1U, .scalar_size = 8U, .is_signed = 0U, .key_len = 1U, .key = { 0x61U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, double_), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 13U, .kind = WL_CODEC_KIND_FLOAT64, .cardinality = WL_CODEC_CARD_PACKED, .wire_type = 2U, .scalar_size = 8U, .is_signed = 0U, .key_len = 1U, .key = { 0x6aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(numbers_t, has_doubles), .offset = (uint16_t)offsetof(numbers_t, doubles), .count_offset = 0U, .capacity_offset = 0U, .max_length = 2U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 14U, .kind = WL_CODEC_KIND_FIXED32, .cardinality = WL_CODEC_CARD_REQUIRED_PACKED, .wire_type = 2U, .scalar_size = 4U, .is_signed = 0U, .key_len = 1U, .key = { 0x72U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(numbers_t, words), .count_offset = 0U, .capacity_offset = 0U, .max_length = 2U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 15U, .kind = WL_CODEC_KIND_INT64, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 8U, .is_signed = 1U, .key_len = 1U, .key = { 0x78U, 0x00U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(numbers_t, has_offset), .offset = (uint16_t)offsetof(numbers_t, offset), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(18446744073709551609), .string_default = NULL, .child = NULL },
  { .number = 16U, .kind = WL_CODEC_KIND_INT32, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 4U, .is_signed = 1U, .key_len = 2U, .key = { 0x80U, 0x01U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(numbers_t, has_minimum), .offset = (uint16_t)offsetof(numbers_t, minimum), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(18446744071562067968), .string_default = NULL, .child = NULL },
  { .number = 17U, .kind = WL_CODEC_KIND_BOOL, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 1U, .is_signed = 0U, .key_len = 2U, .key = { 0x88U, 0x01U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(numbers_t, has_flag), .offset = (uint16_t)offsetof(numbers_t, flag), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(1), .string_default = NULL, .child = NULL },
  { .number = 18U, .kind = WL_CODEC_KIND_INLINE_STRING, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 2U, .key = { 0x92U, 0x01U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(numbers_t, has_text), .offset = (uint16_t)offsetof(numbers_t, text), .count_offset = (uint16_t)(offsetof(numbers_t, text.data) - offsetof(numbers_t, text)), .capacity_offset = 0U, .max_length = 16U, .element_stride = 0U, .default_bits = UINT64_C(8), .string_default = numbers_text_default_str, .child = NULL },
};
static const uint16_t numbers_dense[18] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U };
static const wl_codec_desc_t numbers_desc = {
  .fields = numbers_fields,
  .field_count = 18U,
  .struct_size = sizeof(numbers_t),
  .presence_bytes = 0U,
  .min_number = 1U,
  .max_number = 18U,
  .lookup = WL_CODEC_LOOKUP_DENSE,
  .has_repeated = 0U,
  .dense_map = numbers_dense,
  .clear = NULL,
  .measure = NULL,
  .encode = NULL,
  .decode = NULL
};

void numbers_clear(numbers_t *value) { wl_codec_clear(&numbers_desc, value); }
size_t numbers_encoded_size(const numbers_t *value) { size_t size = 0U; return wl_codec_measure(&numbers_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t numbers_encode(const numbers_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&numbers_desc, value, out, out_capacity, out_length); }
wl_codec_status_t numbers_decode(const uint8_t *input, size_t input_length, numbers_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; numbers_t scratch; return wl_codec_decode(&numbers_desc, input, input_length, out, &scratch); }


#include <limits.h>
#ifdef WL_CODEC_HAS_LOCAL_DECODE
#define WLC_NODE_DECODE(m, ...) wl_codec_decode_local(&m##_desc, __VA_ARGS__)
#else
#define WLC_NODE_DECODE(m, ...) m##_decode(__VA_ARGS__)
#endif
static void device_complete_configure(wl_endpoint_t *core, const wl_endpoint_call_t *call, wl_rpc_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  configure_response_t response;
  const configure_response_t *view = NULL;
  if (result.outcome == WL_RPC_SUCCESS) {
    if (WLC_NODE_DECODE(configure_response, body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((device_configure_cb_t)call->callback)((device_node_t *)core, result, view);
}
static void device_complete_configure_ex(wl_endpoint_t *core, const wl_endpoint_call_t *call, wl_rpc_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  configure_response_t response;
  const configure_response_t *view = NULL;
  if (result.outcome == WL_RPC_SUCCESS) {
    if (WLC_NODE_DECODE(configure_response, body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((device_configure_ex_cb_t)call->callback)(call->user, result, view);
}
static wl_err_t device_call_configure_start(device_node_t *self, const configure_request_t *request, uint32_t now, uint32_t timeout, uint8_t has_now, wl_endpoint_complete_fn complete, void (*callback)(void), void *user, wl_call_handle_t *out) {
  if (out != NULL) *out = (wl_call_handle_t){0};
  if (self == NULL || request == NULL || callback == NULL || timeout > INT32_MAX) return WL_ERR_INVALID_ARG;
  if (timeout == 0U) timeout = 500U;
  uint8_t slot; uint8_t *payload; size_t capacity;
  wl_err_t error = wl_endpoint_call_claim(&self->node.core, &slot, &payload, &capacity);
  if (error != WL_OK) return error;
  size_t length = 0U;
  if (configure_request_encode(request, payload, capacity, &length) != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  return wl_endpoint_call_commit(&self->node.core, slot, 5U, 6U, length, now, timeout, has_now, complete, callback, user, out);
}
wl_err_t device_call_configure(device_node_t *self, const configure_request_t *request, device_configure_cb_t callback) {
  return device_call_configure_start(self, request, 0U, 0U, 0U, device_complete_configure, (void (*)(void))callback, self, NULL);
}
wl_err_t device_call_configure_ex(device_node_t *self, const configure_request_t *request, uint32_t now, uint32_t timeout_ms, device_configure_ex_cb_t callback, void *user, wl_call_handle_t *out) {
  return device_call_configure_start(self, request, now, timeout_ms, 1U, device_complete_configure_ex, (void (*)(void))callback, user, out);
}
static void device_complete_get_info(wl_endpoint_t *core, const wl_endpoint_call_t *call, wl_rpc_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  info_response_t response;
  const info_response_t *view = NULL;
  if (result.outcome == WL_RPC_SUCCESS) {
    if (WLC_NODE_DECODE(info_response, body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((device_get_info_cb_t)call->callback)((device_node_t *)core, result, view);
}
static void device_complete_get_info_ex(wl_endpoint_t *core, const wl_endpoint_call_t *call, wl_rpc_result_t result, const uint8_t *body, size_t length) {
  (void)core;
  info_response_t response;
  const info_response_t *view = NULL;
  if (result.outcome == WL_RPC_SUCCESS) {
    if (WLC_NODE_DECODE(info_response, body, length, &response) == WL_CODEC_OK) view = &response;
    else result = (wl_rpc_result_t){WL_RPC_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  }
  ((device_get_info_ex_cb_t)call->callback)(call->user, result, view);
}
static wl_err_t device_call_get_info_start(device_node_t *self, const info_request_t *request, uint32_t now, uint32_t timeout, uint8_t has_now, wl_endpoint_complete_fn complete, void (*callback)(void), void *user, wl_call_handle_t *out) {
  if (out != NULL) *out = (wl_call_handle_t){0};
  if (self == NULL || request == NULL || callback == NULL || timeout > INT32_MAX) return WL_ERR_INVALID_ARG;
  if (timeout == 0U) timeout = 500U;
  uint8_t slot; uint8_t *payload; size_t capacity;
  wl_err_t error = wl_endpoint_call_claim(&self->node.core, &slot, &payload, &capacity);
  if (error != WL_OK) return error;
  size_t length = 0U;
  if (info_request_encode(request, payload, capacity, &length) != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  return wl_endpoint_call_commit(&self->node.core, slot, 2U, 4U, length, now, timeout, has_now, complete, callback, user, out);
}
wl_err_t device_call_get_info(device_node_t *self, const info_request_t *request, device_get_info_cb_t callback) {
  return device_call_get_info_start(self, request, 0U, 0U, 0U, device_complete_get_info, (void (*)(void))callback, self, NULL);
}
wl_err_t device_call_get_info_ex(device_node_t *self, const info_request_t *request, uint32_t now, uint32_t timeout_ms, device_get_info_ex_cb_t callback, void *user, wl_call_handle_t *out) {
  return device_call_get_info_start(self, request, now, timeout_ms, 1U, device_complete_get_info_ex, (void (*)(void))callback, user, out);
}
static wl_err_t device_route(wl_endpoint_t *core, const wl_wire_frame_t *frame) {
  device_node_t *self = (device_node_t *)core;
  (void)self;
  switch (frame->message_id) {
  case 6U: return wl_endpoint_response(core, frame);
  case 4U: return wl_endpoint_response(core, frame);
  default: return WL_ERR_NOT_SUPPORTED;
  }
}
static wl_err_t device_sync_write(wl_node_t *node, const uint8_t *data, size_t length) {
  device_node_t *self = (device_node_t *)node;
  return self->tx_write(self, data, length);
}
static wl_err_t device_bind_impl(device_node_t *self, uint32_t instance, wl_transport_t *transport, device_tx_write_fn tx_write) {
  if (self == NULL) return WL_ERR_INVALID_ARG;
  if (transport == NULL && tx_write == NULL) return WL_ERR_INVALID_ARG;
  self->tx_write = tx_write;
  const wl_node_layout_t layout = {
    .instance = instance, .has_tx = 1, .rx_slots = 2U,
    .route = device_route, .user = NULL,
    .rx_storage = &self->rx[0][0], .rx_storage_size = sizeof(self->rx),
    .rx_frame_max = DEVICE_RX_FRAME_MAX, .tx_frame_max = DEVICE_TX_FRAME_MAX,
    .rx_wire_max = DEVICE_RX_WIRE_MAX, .tx_wire_max = DEVICE_TX_WIRE_MAX,
    .storage = {
          .requests = &self->requests[0][0], .request_stride = 222U,
          .replies = NULL, .reply_stride = 0U,
          .message = NULL, .message_capacity = 0U,
    }
  };
  return transport != NULL ? wl_node_bind_transport(&self->node, &layout, transport)
                           : wl_node_bind_sync(&self->node, &layout, device_sync_write);
}
wl_err_t device_bind(device_node_t *self, wl_transport_t *transport) {
  if (transport == NULL) return WL_ERR_INVALID_ARG;
  return device_bind_impl(self, 0U, transport, NULL);
}
wl_err_t device_bind_instance(device_node_t *self, uint32_t instance, wl_transport_t *transport) {
  if (instance == 0U) return WL_ERR_INVALID_ARG;
  if (transport == NULL) return WL_ERR_INVALID_ARG;
  return device_bind_impl(self, instance, transport, NULL);
}
wl_err_t device_bind_sync(device_node_t *self, device_tx_write_fn tx_write) {
  return device_bind_impl(self, 0U, NULL, tx_write);
}
wl_err_t device_bind_sync_instance(device_node_t *self, uint32_t instance, device_tx_write_fn tx_write) {
  if (instance == 0U) return WL_ERR_INVALID_ARG;
  return device_bind_impl(self, instance, NULL, tx_write);
}
