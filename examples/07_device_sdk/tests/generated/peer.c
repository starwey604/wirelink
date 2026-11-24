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

void peer_info_request_clear(peer_info_request_t *value) { wl_codec_clear(&peer_info_request_desc, value); }
wl_codec_status_t peer_info_request_decode(const uint8_t *input, size_t input_length, peer_info_request_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; peer_info_request_t scratch; return wl_codec_decode(&peer_info_request_desc, input, input_length, out, &scratch); }

static void peer_settings_wlc_clear_v(void *value);
static wl_codec_status_t peer_settings_wlc_measure_v(const void *value, size_t *out_size);
static wl_codec_status_t peer_settings_wlc_encode_v(const void *value, uint8_t *out, size_t capacity, size_t *length);
static wl_codec_status_t peer_settings_wlc_decode_v(const uint8_t *input, size_t length, void *out);
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
  .measure = peer_settings_wlc_measure_v,
  .encode = peer_settings_wlc_encode_v,
  .decode = peer_settings_wlc_decode_v
};

static void peer_settings_wlc_clear_v(void *value) { wl_codec_clear(&peer_settings_desc, value); }
static wl_codec_status_t peer_settings_wlc_measure_v(const void *value, size_t *out_size) { return wl_codec_measure(&peer_settings_desc, value, out_size); }
static wl_codec_status_t peer_settings_wlc_encode_v(const void *value, uint8_t *out, size_t capacity, size_t *length) { return wl_codec_encode(&peer_settings_desc, value, out, capacity, length); }
static wl_codec_status_t peer_settings_wlc_decode_v(const uint8_t *input, size_t length, void *out) { peer_settings_t scratch; return wl_codec_decode(&peer_settings_desc, input, length, out, &scratch); }

void peer_settings_clear(peer_settings_t *value) { wl_codec_clear(&peer_settings_desc, value); }
size_t peer_settings_encoded_size(const peer_settings_t *value) { size_t size = 0U; return wl_codec_measure(&peer_settings_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t peer_settings_encode(const peer_settings_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&peer_settings_desc, value, out, out_capacity, out_length); }
wl_codec_status_t peer_settings_decode(const uint8_t *input, size_t input_length, peer_settings_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; peer_settings_t scratch; return wl_codec_decode(&peer_settings_desc, input, input_length, out, &scratch); }

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

void peer_info_response_clear(peer_info_response_t *value) { wl_codec_clear(&peer_info_response_desc, value); }
size_t peer_info_response_encoded_size(const peer_info_response_t *value) { size_t size = 0U; return wl_codec_measure(&peer_info_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t peer_info_response_encode(const peer_info_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&peer_info_response_desc, value, out, out_capacity, out_length); }

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

void peer_configure_request_clear(peer_configure_request_t *value) { wl_codec_clear(&peer_configure_request_desc, value); }
wl_codec_status_t peer_configure_request_decode(const uint8_t *input, size_t input_length, peer_configure_request_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; peer_configure_request_t scratch; return wl_codec_decode(&peer_configure_request_desc, input, input_length, out, &scratch); }

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

void peer_configure_response_clear(peer_configure_response_t *value) { wl_codec_clear(&peer_configure_response_desc, value); }
size_t peer_configure_response_encoded_size(const peer_configure_response_t *value) { size_t size = 0U; return wl_codec_measure(&peer_configure_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t peer_configure_response_encode(const peer_configure_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&peer_configure_response_desc, value, out, out_capacity, out_length); }

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
#ifdef WL_CODEC_HAS_LOCAL_DECODE
#define WLC_NODE_DECODE(m, ...) wl_codec_decode_local(&m##_desc, __VA_ARGS__)
#else
#define WLC_NODE_DECODE(m, ...) m##_decode(__VA_ARGS__)
#endif
static wl_err_t device_peer_route(wl_endpoint_t *core, const wl_wire_frame_t *frame) {
  device_peer_node_t *self = (device_peer_node_t *)core;
  (void)self;
  switch (frame->message_id) {
  case 5U: {
    if (frame->kind != WL_WIRE_RPC_REQ) return WL_ERR_CORRUPT_PAYLOAD;
    if (self->handlers.on_configure == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_rpc_metadata_t metadata;
    wl_err_t error = wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
    if (error != WL_OK) return error;
    uint8_t slot; uint8_t *payload; size_t capacity;
    error = wl_endpoint_reply_claim(core, 6U, &metadata, &slot, &payload, &capacity);
    if (error != WL_OK) return error;
    peer_configure_request_t request;
    if (WLC_NODE_DECODE(peer_configure_request, metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_endpoint_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    peer_configure_response_clear(&self->reply.configure);
    int32_t status = self->handlers.on_configure(self->node.user, &request, &self->reply.configure);
    if (status != 0) return wl_endpoint_reply_error(core, slot, status);
    size_t length = 0U;
    if (peer_configure_response_encode(&self->reply.configure, payload, capacity, &length) != WL_CODEC_OK) {
      wl_endpoint_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    return wl_endpoint_reply_commit(core, slot, length);
  }
  case 2U: {
    if (frame->kind != WL_WIRE_RPC_REQ) return WL_ERR_CORRUPT_PAYLOAD;
    if (self->handlers.on_get_info == NULL) return WL_ERR_NOT_SUPPORTED;
    wl_rpc_metadata_t metadata;
    wl_err_t error = wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
    if (error != WL_OK) return error;
    uint8_t slot; uint8_t *payload; size_t capacity;
    error = wl_endpoint_reply_claim(core, 4U, &metadata, &slot, &payload, &capacity);
    if (error != WL_OK) return error;
    peer_info_request_t request;
    if (peer_info_request_decode(metadata.body, metadata.length, &request) != WL_CODEC_OK) {
      wl_endpoint_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    peer_info_response_clear(&self->reply.get_info);
    int32_t status = self->handlers.on_get_info(self->node.user, &request, &self->reply.get_info);
    if (status != 0) return wl_endpoint_reply_error(core, slot, status);
    size_t length = 0U;
    if (peer_info_response_encode(&self->reply.get_info, payload, capacity, &length) != WL_CODEC_OK) {
      wl_endpoint_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
    }
    return wl_endpoint_reply_commit(core, slot, length);
  }
  default: return WL_ERR_NOT_SUPPORTED;
  }
}
static wl_err_t device_peer_sync_write(wl_node_t *node, const uint8_t *data, size_t length) {
  device_peer_node_t *self = (device_peer_node_t *)node;
  return self->tx_write(self, data, length);
}
static wl_err_t device_peer_bind_impl(device_peer_node_t *self, uint32_t instance, wl_transport_t *transport, device_peer_tx_write_fn tx_write, const device_peer_handlers_t *handlers, void *user) {
  if (self == NULL) return WL_ERR_INVALID_ARG;
  if (transport == NULL && tx_write == NULL) return WL_ERR_INVALID_ARG;
  if (handlers == NULL) return WL_ERR_INVALID_ARG;
  self->handlers = *handlers;
  self->tx_write = tx_write;
  const wl_node_layout_t layout = {
    .instance = instance, .has_tx = 1, .rx_slots = 2U,
    .route = device_peer_route, .user = user,
    .rx_storage = &self->rx[0][0], .rx_storage_size = sizeof(self->rx),
    .rx_frame_max = DEVICE_PEER_RX_FRAME_MAX, .tx_frame_max = DEVICE_PEER_TX_FRAME_MAX,
    .rx_wire_max = DEVICE_PEER_RX_WIRE_MAX, .tx_wire_max = DEVICE_PEER_TX_WIRE_MAX,
    .storage = {
          .requests = NULL, .request_stride = 0U,
          .replies = &self->replies[0][0], .reply_stride = 228U,
          .message = NULL, .message_capacity = 0U,
    }
  };
  return transport != NULL ? wl_node_bind_transport(&self->node, &layout, transport)
                           : wl_node_bind_sync(&self->node, &layout, device_peer_sync_write);
}
wl_err_t device_peer_bind(device_peer_node_t *self, wl_transport_t *transport, const device_peer_handlers_t *handlers, void *user) {
  if (transport == NULL) return WL_ERR_INVALID_ARG;
  return device_peer_bind_impl(self, 0U, transport, NULL, handlers, user);
}
wl_err_t device_peer_bind_instance(device_peer_node_t *self, uint32_t instance, wl_transport_t *transport, const device_peer_handlers_t *handlers, void *user) {
  if (instance == 0U) return WL_ERR_INVALID_ARG;
  if (transport == NULL) return WL_ERR_INVALID_ARG;
  return device_peer_bind_impl(self, instance, transport, NULL, handlers, user);
}
wl_err_t device_peer_bind_sync(device_peer_node_t *self, device_peer_tx_write_fn tx_write, const device_peer_handlers_t *handlers, void *user) {
  return device_peer_bind_impl(self, 0U, NULL, tx_write, handlers, user);
}
wl_err_t device_peer_bind_sync_instance(device_peer_node_t *self, uint32_t instance, device_peer_tx_write_fn tx_write, const device_peer_handlers_t *handlers, void *user) {
  if (instance == 0U) return WL_ERR_INVALID_ARG;
  return device_peer_bind_impl(self, instance, NULL, tx_write, handlers, user);
}
