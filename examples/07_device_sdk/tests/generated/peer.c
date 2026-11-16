/* SPDX-License-Identifier: Apache-2.0 */
#include "peer.h"
#include <stddef.h>
#include <string.h>

#if PEER_INFO_REQUEST_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if PEER_SETTINGS_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if PEER_INFO_RESPONSE_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if PEER_CONFIGURE_REQUEST_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if PEER_CONFIGURE_RESPONSE_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if PEER_NUMBERS_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif

static const wl_codec_desc_t peer_info_request_desc;
static const wl_codec_desc_t peer_settings_desc;
static const wl_codec_desc_t peer_info_response_desc;
static const wl_codec_desc_t peer_configure_request_desc;
static const wl_codec_desc_t peer_configure_response_desc;

void peer_info_request_clear(peer_info_request_t *value) { wl_codec_clear(&peer_info_request_desc, value); }
wl_codec_status_t peer_info_request_decode(const uint8_t *input, size_t input_length, peer_info_request_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; peer_info_request_t scratch; return wl_codec_decode(&peer_info_request_desc, input, input_length, out, &scratch); }

static const wl_codec_desc_t peer_info_request_desc = {
  .fields = NULL,
  .field_count = 0U,
  .struct_size = sizeof(peer_info_request_t),
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

void peer_settings_clear(peer_settings_t *value) { wl_codec_clear(&peer_settings_desc, value); }
size_t peer_settings_encoded_size(const peer_settings_t *value) { size_t size = 0U; return wl_codec_measure(&peer_settings_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t peer_settings_encode(const peer_settings_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&peer_settings_desc, value, out, out_capacity, out_length); }
wl_codec_status_t peer_settings_decode(const uint8_t *input, size_t input_length, peer_settings_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; peer_settings_t scratch; return wl_codec_decode(&peer_settings_desc, input, input_length, out, &scratch); }

static void peer_settings_wlc_clear_v(void *value) { wl_codec_clear(&peer_settings_desc, value); }
static wl_codec_status_t peer_settings_wlc_encode_v(const void *value, uint8_t *out, size_t capacity, size_t *length) { return wl_codec_encode(&peer_settings_desc, value, out, capacity, length); }
static wl_codec_status_t peer_settings_wlc_decode_v(const uint8_t *input, size_t length, void *out) { peer_settings_t scratch; return wl_codec_decode(&peer_settings_desc, input, length, out, &scratch); }
static const char peer_settings_label_default_str[] = "\xE8""\xAE""\xBE""\xE5""\xA4""\x87";
static const wl_codec_field_t peer_settings_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_INLINE_STRING, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(peer_settings_t, has_label), .offset = (uint16_t)offsetof(peer_settings_t, label), .count_offset = (uint16_t)(offsetof(peer_settings_t, label.data) - offsetof(peer_settings_t, label)), .capacity_offset = 0U, .max_length = 12U, .element_stride = 0U, .default_bits = UINT64_C(6), .string_default = peer_settings_label_default_str, .child = NULL },
  { .number = 2U, .kind = WL_CODEC_KIND_BOOL, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 1U, .is_signed = 0U, .key_len = 1U, .key = { 0x10U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(peer_settings_t, enabled), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 3U, .kind = WL_CODEC_KIND_ENUM, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 4U, .is_signed = 1U, .key_len = 1U, .key = { 0x18U, 0x00U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(peer_settings_t, has_mode), .offset = (uint16_t)offsetof(peer_settings_t, mode), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(1), .string_default = NULL, .child = NULL },
  { .number = 4U, .kind = WL_CODEC_KIND_INLINE_BYTES, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x22U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(peer_settings_t, token), .count_offset = (uint16_t)(offsetof(peer_settings_t, token.data) - offsetof(peer_settings_t, token)), .capacity_offset = 0U, .max_length = 8U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 5U, .kind = WL_CODEC_KIND_FLOAT32, .cardinality = WL_CODEC_CARD_REQUIRED_PACKED, .wire_type = 2U, .scalar_size = 4U, .is_signed = 0U, .key_len = 1U, .key = { 0x2aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(peer_settings_t, gains), .count_offset = 0U, .capacity_offset = 0U, .max_length = 3U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 6U, .kind = WL_CODEC_KIND_FIXED64, .cardinality = WL_CODEC_CARD_PACKED, .wire_type = 2U, .scalar_size = 8U, .is_signed = 0U, .key_len = 1U, .key = { 0x32U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(peer_settings_t, has_counters), .offset = (uint16_t)offsetof(peer_settings_t, counters), .count_offset = 0U, .capacity_offset = 0U, .max_length = 2U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 7U, .kind = WL_CODEC_KIND_INT64, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 8U, .is_signed = 1U, .key_len = 1U, .key = { 0x38U, 0x00U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(peer_settings_t, has_floor), .offset = (uint16_t)offsetof(peer_settings_t, floor), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(9223372036854775808), .string_default = NULL, .child = NULL },
  { .number = 8U, .kind = WL_CODEC_KIND_UINT64, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 8U, .is_signed = 0U, .key_len = 1U, .key = { 0x40U, 0x00U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(peer_settings_t, has_ceiling), .offset = (uint16_t)offsetof(peer_settings_t, ceiling), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(18446744073709551615), .string_default = NULL, .child = NULL },
  { .number = 9U, .kind = WL_CODEC_KIND_INLINE_STRING, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x4aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(peer_settings_t, has_class_), .offset = (uint16_t)offsetof(peer_settings_t, class_), .count_offset = (uint16_t)(offsetof(peer_settings_t, class_.data) - offsetof(peer_settings_t, class_)), .capacity_offset = 0U, .max_length = 8U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 10U, .kind = WL_CODEC_KIND_UINT32, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 2U, .is_signed = 0U, .key_len = 1U, .key = { 0x50U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(peer_settings_t, has_timeout), .offset = (uint16_t)offsetof(peer_settings_t, timeout), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
};
static const uint16_t peer_settings_dense[10] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U };
static wl_codec_status_t peer_settings_wlc_measure_generic_v(const void *value, size_t *out_size) { return wl_codec_measure(&peer_settings_desc, value, out_size); }
static const wl_codec_desc_t peer_settings_desc = {
  .fields = peer_settings_fields,
  .field_count = 10U,
  .struct_size = sizeof(peer_settings_t),
  .presence_bytes = 0U,
  .min_number = 1U,
  .max_number = 10U,
  .lookup = WL_CODEC_LOOKUP_DENSE,
  .has_repeated = 0U,
  .dense_map = peer_settings_dense,
  .clear = peer_settings_wlc_clear_v,
  .measure = peer_settings_wlc_measure_generic_v,
  .encode = peer_settings_wlc_encode_v,
  .decode = peer_settings_wlc_decode_v
};

void peer_info_response_clear(peer_info_response_t *value) { wl_codec_clear(&peer_info_response_desc, value); }
size_t peer_info_response_encoded_size(const peer_info_response_t *value) { size_t size = 0U; return wl_codec_measure(&peer_info_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t peer_info_response_encode(const peer_info_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&peer_info_response_desc, value, out, out_capacity, out_length); }

static const wl_codec_field_t peer_info_response_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_INLINE_STRING, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(peer_info_response_t, name), .count_offset = (uint16_t)(offsetof(peer_info_response_t, name.data) - offsetof(peer_info_response_t, name)), .capacity_offset = 0U, .max_length = 32U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 2U, .kind = WL_CODEC_KIND_MESSAGE, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x12U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(peer_info_response_t, settings), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = &peer_settings_desc },
};
static const wl_codec_desc_t peer_info_response_desc = {
  .fields = peer_info_response_fields,
  .field_count = 2U,
  .struct_size = sizeof(peer_info_response_t),
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

void peer_configure_request_clear(peer_configure_request_t *value) { wl_codec_clear(&peer_configure_request_desc, value); }
wl_codec_status_t peer_configure_request_decode(const uint8_t *input, size_t input_length, peer_configure_request_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; peer_configure_request_t scratch; return wl_codec_decode(&peer_configure_request_desc, input, input_length, out, &scratch); }

static const wl_codec_field_t peer_configure_request_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_MESSAGE, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(peer_configure_request_t, settings), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = &peer_settings_desc },
  { .number = 2U, .kind = WL_CODEC_KIND_INLINE_BYTES, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x12U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(peer_configure_request_t, has_opaque), .offset = (uint16_t)offsetof(peer_configure_request_t, opaque), .count_offset = (uint16_t)(offsetof(peer_configure_request_t, opaque.data) - offsetof(peer_configure_request_t, opaque)), .capacity_offset = 0U, .max_length = 4U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 3U, .kind = WL_CODEC_KIND_MESSAGE, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x1aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(peer_configure_request_t, has_backup), .offset = (uint16_t)offsetof(peer_configure_request_t, backup), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = &peer_settings_desc },
};
static const wl_codec_desc_t peer_configure_request_desc = {
  .fields = peer_configure_request_fields,
  .field_count = 3U,
  .struct_size = sizeof(peer_configure_request_t),
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

void peer_configure_response_clear(peer_configure_response_t *value) { wl_codec_clear(&peer_configure_response_desc, value); }
size_t peer_configure_response_encoded_size(const peer_configure_response_t *value) { size_t size = 0U; return wl_codec_measure(&peer_configure_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t peer_configure_response_encode(const peer_configure_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&peer_configure_response_desc, value, out, out_capacity, out_length); }

static const wl_codec_field_t peer_configure_response_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_MESSAGE, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(peer_configure_response_t, settings), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = &peer_settings_desc },
  { .number = 2U, .kind = WL_CODEC_KIND_INLINE_BYTES, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x12U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(peer_configure_response_t, has_opaque), .offset = (uint16_t)offsetof(peer_configure_response_t, opaque), .count_offset = (uint16_t)(offsetof(peer_configure_response_t, opaque.data) - offsetof(peer_configure_response_t, opaque)), .capacity_offset = 0U, .max_length = 4U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 3U, .kind = WL_CODEC_KIND_MESSAGE, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x1aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(peer_configure_response_t, has_backup), .offset = (uint16_t)offsetof(peer_configure_response_t, backup), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = &peer_settings_desc },
  { .number = 4U, .kind = WL_CODEC_KIND_UINT32, .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 4U, .is_signed = 0U, .key_len = 1U, .key = { 0x20U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(peer_configure_response_t, count), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
};
static const wl_codec_desc_t peer_configure_response_desc = {
  .fields = peer_configure_response_fields,
  .field_count = 4U,
  .struct_size = sizeof(peer_configure_response_t),
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

void peer_numbers_clear(peer_numbers_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  value->offset = (int64_t)(-7LL);
  value->minimum = (int32_t)(-2147483648LL);
  value->flag = true;
  value->text.length = 8U;
  memcpy(value->text.data, "\x61""\x22""\x40""\x4E""\x41""\x4D""\x45""\x40", 8U);
}


#include <limits.h>
static void device_peer_error(void *user, uint16_t id, wl_err_t error) {
  device_peer_node_t *self = user;
  if (self->on_error != NULL) self->on_error(self->user, id, error);
}
static wl_err_t device_peer_route(wl_kernel_t *core, const wl_kernel_frame_t *frame) {
  device_peer_node_t *self = (device_peer_node_t *)core;
  (void)self;
  switch (frame->message_id) {
  case 5U: {
    if (frame->kind != WL_WIRE_RPC_REQ) return WL_ERR_CORRUPT_PAYLOAD;
    if (self->handlers.on_configure == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_kernel_rpc_metadata_t metadata;
    wl_err_t error = wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
    if (error != WL_OK) return error;
    uint8_t slot; uint8_t *payload; size_t capacity;
    error = wl_kernel_reply_claim(core, 6U, &metadata, &slot, &payload, &capacity);
    if (error != WL_OK) return error;
    peer_configure_request_t request;
    if (peer_configure_request_decode(metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    peer_configure_response_clear(&self->reply.configure);
    int32_t status = self->handlers.on_configure(self->user, &request, &self->reply.configure);
    if (status != 0) return wl_kernel_reply_error(core, slot, status);
    size_t length = 0U;
    if (peer_configure_response_encode(&self->reply.configure, payload, capacity, &length) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    return wl_kernel_reply_commit(core, slot, length);
  }
  case 2U: {
    if (frame->kind != WL_WIRE_RPC_REQ) return WL_ERR_CORRUPT_PAYLOAD;
    if (self->handlers.on_get_info == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_kernel_rpc_metadata_t metadata;
    wl_err_t error = wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
    if (error != WL_OK) return error;
    uint8_t slot; uint8_t *payload; size_t capacity;
    error = wl_kernel_reply_claim(core, 4U, &metadata, &slot, &payload, &capacity);
    if (error != WL_OK) return error;
    peer_info_request_t request;
    if (peer_info_request_decode(metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    peer_info_response_clear(&self->reply.get_info);
    int32_t status = self->handlers.on_get_info(self->user, &request, &self->reply.get_info);
    if (status != 0) return wl_kernel_reply_error(core, slot, status);
    size_t length = 0U;
    if (peer_info_response_encode(&self->reply.get_info, payload, capacity, &length) != WL_CODEC_OK) {
      wl_kernel_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    return wl_kernel_reply_commit(core, slot, length);
  }
  default: return WL_ERR_NOT_SUPPORTED;
  }
}
wl_err_t device_peer_init(device_peer_node_t *self, wl_packet_port_t *port, const device_peer_handlers_t *handlers, void *user) {
  if (self == NULL || port == NULL) return WL_ERR_INVALID_ARG;
  if (handlers == NULL) return WL_ERR_INVALID_ARG;
  const device_peer_handlers_t saved = *handlers;
  memset(self, 0, sizeof(*self));
  self->handlers = saved; self->user = user;
  wl_kernel_config_t config = {.port = port, .route = device_peer_route, .on_error = device_peer_error, .user = self,
      .storage = {
          .requests = NULL, .request_stride = 0U,
          .replies = &self->replies[0][0], .reply_stride = 228U,
          .message = NULL, .message_capacity = 0U,
      }};
  return wl_kernel_init(&self->core, &config);
}
wl_err_t device_peer_step(device_peer_node_t *self, uint32_t now) {
  return self == NULL ? WL_ERR_INVALID_ARG : wl_kernel_step(&self->core, now, WL_KERNEL_BUDGET);
}
wl_err_t device_peer_close(device_peer_node_t *self) {
  if (self == NULL) return WL_ERR_INVALID_ARG;
  wl_err_t error = wl_kernel_close(&self->core);
  return error;
}
int device_peer_quiescent(const device_peer_node_t *self) {
  return self != NULL && wl_kernel_quiescent(&self->core);
}
