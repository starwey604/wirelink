/* SPDX-License-Identifier: Apache-2.0 */
#ifndef DEVICE_KERNEL_CODECS_H
#define DEVICE_KERNEL_CODECS_H
#include "device.h"
static inline void device_rpc_info_request_clear(info_request_t *);
static inline size_t device_rpc_info_request_encoded_size(const info_request_t *);
static inline wl_codec_status_t device_rpc_info_request_encode(const info_request_t *, uint8_t *, size_t, size_t *);
static inline wl_codec_status_t device_rpc_info_request_decode(const uint8_t *, size_t, info_request_t *);
static inline void device_rpc_info_request_value_clear(info_request_value_t *);
static inline size_t device_rpc_info_request_value_encoded_size(const info_request_value_t *);
static inline wl_codec_status_t device_rpc_info_request_value_encode(const info_request_value_t *, uint8_t *, size_t, size_t *);
static inline wl_codec_status_t device_rpc_info_request_value_decode(const uint8_t *, size_t, info_request_value_t *);
static inline wl_codec_status_t device_rpc_info_request_value_from_view(const info_request_t *, info_request_value_t *);
static inline wl_codec_status_t device_rpc_info_request_value_to_view(const info_request_value_t *, info_request_t *);
static inline void device_rpc_settings_clear(settings_t *);
static inline size_t device_rpc_settings_encoded_size(const settings_t *);
static inline wl_codec_status_t device_rpc_settings_encode(const settings_t *, uint8_t *, size_t, size_t *);
static inline wl_codec_status_t device_rpc_settings_decode(const uint8_t *, size_t, settings_t *);
static inline void device_rpc_settings_value_clear(settings_value_t *);
static inline size_t device_rpc_settings_value_encoded_size(const settings_value_t *);
static inline wl_codec_status_t device_rpc_settings_value_encode(const settings_value_t *, uint8_t *, size_t, size_t *);
static inline wl_codec_status_t device_rpc_settings_value_decode(const uint8_t *, size_t, settings_value_t *);
static inline wl_codec_status_t device_rpc_settings_value_from_view(const settings_t *, settings_value_t *);
static inline wl_codec_status_t device_rpc_settings_value_to_view(const settings_value_t *, settings_t *);
static inline void device_rpc_info_response_clear(info_response_t *);
static inline size_t device_rpc_info_response_encoded_size(const info_response_t *);
static inline wl_codec_status_t device_rpc_info_response_encode(const info_response_t *, uint8_t *, size_t, size_t *);
static inline wl_codec_status_t device_rpc_info_response_decode(const uint8_t *, size_t, info_response_t *);
static inline void device_rpc_info_response_value_clear(info_response_value_t *);
static inline size_t device_rpc_info_response_value_encoded_size(const info_response_value_t *);
static inline wl_codec_status_t device_rpc_info_response_value_encode(const info_response_value_t *, uint8_t *, size_t, size_t *);
static inline wl_codec_status_t device_rpc_info_response_value_decode(const uint8_t *, size_t, info_response_value_t *);
static inline wl_codec_status_t device_rpc_info_response_value_from_view(const info_response_t *, info_response_value_t *);
static inline wl_codec_status_t device_rpc_info_response_value_to_view(const info_response_value_t *, info_response_t *);
static inline void device_rpc_configure_request_clear(configure_request_t *);
static inline size_t device_rpc_configure_request_encoded_size(const configure_request_t *);
static inline wl_codec_status_t device_rpc_configure_request_encode(const configure_request_t *, uint8_t *, size_t, size_t *);
static inline wl_codec_status_t device_rpc_configure_request_decode(const uint8_t *, size_t, configure_request_t *);
static inline void device_rpc_configure_request_value_clear(configure_request_value_t *);
static inline size_t device_rpc_configure_request_value_encoded_size(const configure_request_value_t *);
static inline wl_codec_status_t device_rpc_configure_request_value_encode(const configure_request_value_t *, uint8_t *, size_t, size_t *);
static inline wl_codec_status_t device_rpc_configure_request_value_decode(const uint8_t *, size_t, configure_request_value_t *);
static inline wl_codec_status_t device_rpc_configure_request_value_from_view(const configure_request_t *, configure_request_value_t *);
static inline wl_codec_status_t device_rpc_configure_request_value_to_view(const configure_request_value_t *, configure_request_t *);
static inline void device_rpc_configure_response_clear(configure_response_t *);
static inline size_t device_rpc_configure_response_encoded_size(const configure_response_t *);
static inline wl_codec_status_t device_rpc_configure_response_encode(const configure_response_t *, uint8_t *, size_t, size_t *);
static inline wl_codec_status_t device_rpc_configure_response_decode(const uint8_t *, size_t, configure_response_t *);
static inline void device_rpc_configure_response_value_clear(configure_response_value_t *);
static inline size_t device_rpc_configure_response_value_encoded_size(const configure_response_value_t *);
static inline wl_codec_status_t device_rpc_configure_response_value_encode(const configure_response_value_t *, uint8_t *, size_t, size_t *);
static inline wl_codec_status_t device_rpc_configure_response_value_decode(const uint8_t *, size_t, configure_response_value_t *);
static inline wl_codec_status_t device_rpc_configure_response_value_from_view(const configure_response_t *, configure_response_value_t *);
static inline wl_codec_status_t device_rpc_configure_response_value_to_view(const configure_response_value_t *, configure_response_t *);


#include <limits.h>
#include <stddef.h>
#include <string.h>

enum {
  DEVICE_RPC_WLC_OPTIONAL,
  DEVICE_RPC_WLC_REPEATED,
  DEVICE_RPC_WLC_PACKED,
  DEVICE_RPC_WLC_BOOL,
  DEVICE_RPC_WLC_U8,
  DEVICE_RPC_WLC_U16,
  DEVICE_RPC_WLC_U32,
  DEVICE_RPC_WLC_U64,
  DEVICE_RPC_WLC_I8,
  DEVICE_RPC_WLC_I16,
  DEVICE_RPC_WLC_I32,
  DEVICE_RPC_WLC_I64,
  DEVICE_RPC_WLC_F32,
  DEVICE_RPC_WLC_F64,
  DEVICE_RPC_WLC_FLOAT32,
  DEVICE_RPC_WLC_FLOAT64,
  DEVICE_RPC_WLC_BYTES,
  DEVICE_RPC_WLC_STRING,
  DEVICE_RPC_WLC_ENUM,
  DEVICE_RPC_WLC_MESSAGE
};
typedef struct device_rpc_wlc_desc device_rpc_wlc_desc_t;
typedef struct {
  uint16_t number;
  uint8_t card, kind, required, wire, key_size;
  size_t value, has, count, capacity, element, packed_count;
  uint16_t max_length;
  uint8_t key[3];
  int64_t signed_default;
  uint64_t unsigned_default;
  const char *string_default;
  const device_rpc_wlc_desc_t *nested;
} device_rpc_wlc_field_t;
enum { DEVICE_RPC_WLC_LOOKUP_LINEAR, DEVICE_RPC_WLC_LOOKUP_DENSE, DEVICE_RPC_WLC_LOOKUP_BINARY };
struct device_rpc_wlc_desc { const device_rpc_wlc_field_t *fields; size_t count; uint8_t lookup; };

static inline wl_codec_status_t device_rpc_wlc_add(size_t *a, size_t b) {
  if (b > SIZE_MAX - *a) return WL_CODEC_ERR_OVERFLOW;
  *a += b;
  return WL_CODEC_OK;
}
static inline size_t device_rpc_wlc_vsize(uint64_t v) {
  size_t n = 1U;
  while (v >= 128U) { v >>= 7U; ++n; }
  return n;
}
static inline void device_rpc_wlc_putv(uint8_t **p, uint64_t v) {
  while (v >= 128U) { *(*p)++ = (uint8_t)(v | 128U); v >>= 7U; }
  *(*p)++ = (uint8_t)v;
}
static wl_codec_status_t device_rpc_wlc_getv(const uint8_t *in, size_t length, size_t *at,
                                  uint64_t *out) {
  size_t start = *at, n = 0U;
  uint64_t v = 0U;
  while (*at < length && n < 10U) {
    uint8_t b = in[(*at)++];
    if (n == 9U && b > 1U) return WL_CODEC_ERR_OVERFLOW;
    v |= (uint64_t)(b & 127U) << (7U * n++);
    if ((b & 128U) == 0U) {
      if (device_rpc_wlc_vsize(v) != *at - start) return WL_CODEC_ERR_MALFORMED;
      *out = v;
      return WL_CODEC_OK;
    }
  }
  return *at == length ? WL_CODEC_ERR_MALFORMED : WL_CODEC_ERR_OVERFLOW;
}
static bool device_rpc_wlc_utf8(const uint8_t *s, size_t n) {
  size_t i = 0U;
  while (i < n) {
    uint8_t a = s[i++];
    if (a < 0x80U) continue;
    size_t need;
    uint32_t v;
    if (a >= 0xC2U && a <= 0xDFU) { need = 1U; v = a & 0x1FU; }
    else if (a >= 0xE0U && a <= 0xEFU) { need = 2U; v = a & 0x0FU; }
    else if (a >= 0xF0U && a <= 0xF4U) { need = 3U; v = a & 0x07U; }
    else return false;
    if (need > n - i) return false;
    while (need-- != 0U) {
      uint8_t b = s[i++];
      if ((b & 0xC0U) != 0x80U) return false;
      v = (v << 6U) | (b & 0x3FU);
    }
    if ((a == 0xE0U && v < 0x800U) ||
        (a == 0xEDU && v >= 0xD800U) ||
        (a == 0xF0U && v < 0x10000U) ||
        (a == 0xF4U && v > 0x10FFFFU)) return false;
  }
  return true;
}
static inline uint8_t device_rpc_wlc_wire(const device_rpc_wlc_field_t *f) {
  return f->wire;
}
static inline uint64_t device_rpc_wlc_z32(int32_t v) {
  return ((uint32_t)v << 1U) ^ (UINT32_C(0) - (uint32_t)(v < 0));
}
static inline uint64_t device_rpc_wlc_z64(int64_t v) {
  return ((uint64_t)v << 1U) ^ (UINT64_C(0) - (uint64_t)(v < 0));
}
static inline int32_t device_rpc_wlc_uz32(uint32_t v) {
  return (int32_t)((v >> 1U) ^ (UINT32_C(0) - (v & 1U)));
}
static inline int64_t device_rpc_wlc_uz64(uint64_t v) {
  return (int64_t)((v >> 1U) ^ (UINT64_C(0) - (v & 1U)));
}
static inline wl_codec_status_t device_rpc_wlc_measure(const device_rpc_wlc_desc_t *, const void *, size_t *);
static wl_codec_status_t device_rpc_wlc_measure_impl(const device_rpc_wlc_desc_t *, const void *, size_t *, bool);
static void device_rpc_wlc_clear(const device_rpc_wlc_desc_t *, void *);
static inline wl_codec_status_t device_rpc_wlc_decode(const device_rpc_wlc_desc_t *, const uint8_t *, size_t,
                                    void *);
static wl_codec_status_t device_rpc_wlc_emit_fields(const device_rpc_wlc_desc_t *, const void *,
                                         uint8_t **);

static wl_codec_status_t device_rpc_wlc_packed_bytes(const device_rpc_wlc_field_t *f, size_t *bytes) {
  if (f->packed_count != 0U && f->element > SIZE_MAX / f->packed_count)
    return WL_CODEC_ERR_OVERFLOW;
  *bytes = f->element * f->packed_count;
  return WL_CODEC_OK;
}
static wl_codec_status_t device_rpc_wlc_body(const device_rpc_wlc_field_t *f, const void *p,
                                  size_t *n, bool validated) {
  (void)p;
  (void)validated;
  *n = 0U;
  switch (f->kind) {
    case DEVICE_RPC_WLC_BOOL: {
      bool v = *(const bool *)p;
      if (v != false && v != true) return WL_CODEC_ERR_INVALID_VALUE;
      *n = 1U;
      return WL_CODEC_OK;
    }
    case DEVICE_RPC_WLC_U16: *n = device_rpc_wlc_vsize(*(const uint16_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U32: *n = device_rpc_wlc_vsize(*(const uint32_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U64: *n = device_rpc_wlc_vsize(*(const uint64_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_ENUM:
      *n = device_rpc_wlc_vsize(device_rpc_wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_I64: *n = device_rpc_wlc_vsize(device_rpc_wlc_z64(*(const int64_t *)p)); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_FLOAT32:
      *n = 4U; return WL_CODEC_OK;
    case DEVICE_RPC_WLC_F64:
      *n = 8U; return WL_CODEC_OK;
    case DEVICE_RPC_WLC_BYTES: {
      const wl_codec_bytes_t *v = p;
      if (f->max_length != 0U && v->length > (size_t)f->max_length)
        return WL_CODEC_ERR_INVALID_VALUE;
      if (v->length != 0U && v->data == NULL) return WL_CODEC_ERR_INVALID_VALUE;
      if (device_rpc_wlc_add(n, device_rpc_wlc_vsize(v->length)) != WL_CODEC_OK)
        return WL_CODEC_ERR_OVERFLOW;
      return device_rpc_wlc_add(n, v->length);
    }
    case DEVICE_RPC_WLC_STRING: {
      const wl_codec_string_t *v = p;
      if (f->max_length != 0U && v->length > (size_t)f->max_length)
        return WL_CODEC_ERR_INVALID_VALUE;
      if (v->length != 0U && v->data == NULL) return WL_CODEC_ERR_INVALID_VALUE;
      if (!validated && !device_rpc_wlc_utf8((const uint8_t *)v->data, v->length)) return WL_CODEC_ERR_UTF8;
      if (device_rpc_wlc_add(n, device_rpc_wlc_vsize(v->length)) != WL_CODEC_OK)
        return WL_CODEC_ERR_OVERFLOW;
      return device_rpc_wlc_add(n, v->length);
    }
    case DEVICE_RPC_WLC_MESSAGE: {
      size_t child;
      wl_codec_status_t s = device_rpc_wlc_measure_impl(f->nested, p, &child, validated);
      if (s != WL_CODEC_OK) return s;
      *n = device_rpc_wlc_vsize(child);
      return device_rpc_wlc_add(n, child);
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
static wl_codec_status_t device_rpc_wlc_measure_impl(const device_rpc_wlc_desc_t *d, const void *value,
                                     size_t *out, bool validated) {
  size_t n = 0U;
  if (d == NULL || value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  for (size_t i = 0U; i < d->count; ++i) {
    const device_rpc_wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
    if (f->card == DEVICE_RPC_WLC_PACKED) {
      size_t bytes;
      wl_codec_status_t s;
      if (!*(const bool *)(base + f->has)) {
        if (f->required != 0U) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
        continue;
      }
      if ((s = device_rpc_wlc_packed_bytes(f, &bytes)) != WL_CODEC_OK) return s;
      if ((s = device_rpc_wlc_add(&n, f->key_size)) != WL_CODEC_OK ||
          (s = device_rpc_wlc_add(&n, device_rpc_wlc_vsize(bytes))) != WL_CODEC_OK ||
          (s = device_rpc_wlc_add(&n, bytes)) != WL_CODEC_OK) return s;
      continue;
    }
    size_t count = 1U;
    if (f->card == DEVICE_RPC_WLC_OPTIONAL) {
      if (!*(const bool *)(base + f->has)) {
        if (f->required != 0U) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
        continue;
      }
    }
    for (size_t j = 0U; j < count; ++j) {
      size_t body;
      const void *p = base + f->value;
      wl_codec_status_t s = device_rpc_wlc_body(f, p, &body, validated);
      if (s != WL_CODEC_OK) return s;
      if ((s = device_rpc_wlc_add(&n, f->key_size)) != WL_CODEC_OK ||
          (s = device_rpc_wlc_add(&n, body)) != WL_CODEC_OK) return s;
    }
  }
  *out = n;
  return WL_CODEC_OK;
}
static inline wl_codec_status_t device_rpc_wlc_measure(const device_rpc_wlc_desc_t *d, const void *value, size_t *out) {
  return device_rpc_wlc_measure_impl(d, value, out, false);
}
static void device_rpc_wlc_clear(const device_rpc_wlc_desc_t *d, void *value) {
  uint8_t *base = value;
  for (size_t i = 0U; i < d->count; ++i) {
    const device_rpc_wlc_field_t *f = &d->fields[i];
    void *p = base + f->value;
    (void)p;
    *(bool *)(base + f->has) = false;
    if (f->card == DEVICE_RPC_WLC_PACKED) {
      memset(p, 0, f->element * f->packed_count);
      continue;
    }
    switch (f->kind) {
    case DEVICE_RPC_WLC_MESSAGE: device_rpc_wlc_clear(f->nested, p); break;
    case DEVICE_RPC_WLC_STRING:
      *(wl_codec_string_t *)p = (wl_codec_string_t){f->string_default, (size_t)f->unsigned_default}; break;
    case DEVICE_RPC_WLC_BYTES: *(wl_codec_bytes_t *)p = (wl_codec_bytes_t){NULL, 0U}; break;
    case DEVICE_RPC_WLC_FLOAT32:
      memset(p, 0, f->element); break;
    case DEVICE_RPC_WLC_BOOL: *(bool *)p = f->unsigned_default != 0U; break;
    case DEVICE_RPC_WLC_ENUM:
      *(int32_t *)p = (int32_t)f->signed_default; break;
    case DEVICE_RPC_WLC_I64: *(int64_t *)p = f->signed_default; break;
    case DEVICE_RPC_WLC_U16: *(uint16_t *)p = (uint16_t)f->unsigned_default; break;
    case DEVICE_RPC_WLC_U32:
      *(uint32_t *)p = (uint32_t)f->unsigned_default; break;
    case DEVICE_RPC_WLC_U64:
    case DEVICE_RPC_WLC_F64:
      *(uint64_t *)p = f->unsigned_default; break;
    default: break;
    }
  }
}
static inline void device_rpc_wlc_put32(uint8_t **p, uint32_t v) {
  *(*p)++ = (uint8_t)(v >> 24U);
  *(*p)++ = (uint8_t)(v >> 16U);
  *(*p)++ = (uint8_t)(v >> 8U);
  *(*p)++ = (uint8_t)v;
}
static inline void device_rpc_wlc_put64(uint8_t **p, uint64_t v) {
  device_rpc_wlc_put32(p, (uint32_t)(v >> 32U));
  device_rpc_wlc_put32(p, (uint32_t)v);
}
static inline void device_rpc_wlc_copy_span(uint8_t **out, const void *data, size_t length) {
  if (length != 0U) { memcpy(*out, data, length); *out += length; }
}
static inline void device_rpc_wlc_put_key(uint8_t **out, const device_rpc_wlc_field_t *f) {
  *(*out)++ = f->key[0];
  if (f->key_size > 1U) {
    *(*out)++ = f->key[1];
    if (f->key_size > 2U) *(*out)++ = f->key[2];
  }
}
static wl_codec_status_t device_rpc_wlc_emit_fixed(uint8_t kind, const void *value,
                                        uint8_t **out) {
  switch (kind) {
    case DEVICE_RPC_WLC_F64: device_rpc_wlc_put64(out, *(const uint64_t *)value); break;
    case DEVICE_RPC_WLC_FLOAT32: {
      uint32_t bits32;
      memcpy(&bits32, value, sizeof(bits32));
      device_rpc_wlc_put32(out, bits32); break;
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
  return WL_CODEC_OK;
}
static wl_codec_status_t device_rpc_wlc_emit_value(const device_rpc_wlc_field_t *f, const void *p,
                                        uint8_t **out) {
  (void)p; (void)out;
  switch (f->kind) {
    case DEVICE_RPC_WLC_BOOL: device_rpc_wlc_putv(out, *(const bool *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U16: device_rpc_wlc_putv(out, *(const uint16_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U32: device_rpc_wlc_putv(out, *(const uint32_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U64: device_rpc_wlc_putv(out, *(const uint64_t *)p); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_ENUM:
      device_rpc_wlc_putv(out, device_rpc_wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_I64: device_rpc_wlc_putv(out, device_rpc_wlc_z64(*(const int64_t *)p)); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_F64:
    case DEVICE_RPC_WLC_FLOAT32:
      return device_rpc_wlc_emit_fixed(f->kind, p, out);
    case DEVICE_RPC_WLC_BYTES: {
      const wl_codec_bytes_t *v = p;
      device_rpc_wlc_putv(out, v->length);
      device_rpc_wlc_copy_span(out, v->data, v->length);
      return WL_CODEC_OK;
    }
    case DEVICE_RPC_WLC_STRING: {
      const wl_codec_string_t *v = p;
      device_rpc_wlc_putv(out, v->length);
      device_rpc_wlc_copy_span(out, v->data, v->length);
      return WL_CODEC_OK;
    }
    case DEVICE_RPC_WLC_MESSAGE: {
      size_t child;
      wl_codec_status_t s = device_rpc_wlc_measure(f->nested, p, &child);
      if (s != WL_CODEC_OK) return s;
      device_rpc_wlc_putv(out, child);
      return device_rpc_wlc_emit_fields(f->nested, p, out);
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
static wl_codec_status_t device_rpc_wlc_emit_packed(const device_rpc_wlc_field_t *f, const void *p,
                                         uint8_t **out) {
  size_t bytes;
  wl_codec_status_t s = device_rpc_wlc_packed_bytes(f, &bytes);
  if (s != WL_CODEC_OK) return s;
  device_rpc_wlc_putv(out, bytes);
  if (f->kind == DEVICE_RPC_WLC_F32 || f->kind == DEVICE_RPC_WLC_FLOAT32) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint32_t bits;
      memcpy(&bits, (const uint8_t *)p + j * f->element, sizeof(bits));
      device_rpc_wlc_put32(out, bits);
    }
    return WL_CODEC_OK;
  }
  if (f->kind == DEVICE_RPC_WLC_F64 || f->kind == DEVICE_RPC_WLC_FLOAT64) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint64_t bits;
      memcpy(&bits, (const uint8_t *)p + j * f->element, sizeof(bits));
      device_rpc_wlc_put64(out, bits);
    }
    return WL_CODEC_OK;
  }
  return WL_CODEC_ERR_INVALID_VALUE;
}
static inline wl_codec_status_t device_rpc_wlc_emit_fields(const device_rpc_wlc_desc_t *d, const void *value,
                                         uint8_t **out) {
  for (size_t i = 0U; i < d->count; ++i) {
    const device_rpc_wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
    if (f->card == DEVICE_RPC_WLC_PACKED) {
      wl_codec_status_t s;
      if (!*(const bool *)(base + f->has)) continue;
      device_rpc_wlc_put_key(out, f);
      if ((s = device_rpc_wlc_emit_packed(f, base + f->value, out)) != WL_CODEC_OK) return s;
      continue;
    }
    size_t count = f->card == DEVICE_RPC_WLC_OPTIONAL && *(const bool *)(base + f->has) ? 1U : 0U;
    for (size_t j = 0U; j < count; ++j) {
      const void *p = base + f->value;
      wl_codec_status_t s;
      device_rpc_wlc_put_key(out, f);
      if ((s = device_rpc_wlc_emit_value(f, p, out)) != WL_CODEC_OK) return s;
    }
  }
  return WL_CODEC_OK;
}

static inline wl_codec_status_t device_rpc_wlc_encode(const device_rpc_wlc_desc_t *d, const void *value,
                                    uint8_t *out, size_t cap, size_t *length) {
  size_t n = 0U;
  wl_codec_status_t s = device_rpc_wlc_measure(d, value, &n);
  if (s != WL_CODEC_OK || length == NULL || (n != 0U && out == NULL))
    return s == WL_CODEC_OK ? WL_CODEC_ERR_INVALID_VALUE : s;
  if (cap < n) return WL_CODEC_ERR_CAPACITY;
  uint8_t *p = out;
  if ((s = device_rpc_wlc_emit_fields(d, value, &p)) != WL_CODEC_OK) return s;
  *length = n;
  return WL_CODEC_OK;
}
static wl_codec_status_t device_rpc_wlc_skip(uint8_t wire, const uint8_t *in, size_t n,
                                  size_t *at) {
  uint64_t length;
  wl_codec_status_t s;
  if (wire == 0U) return device_rpc_wlc_getv(in, n, at, &length);
  if (wire == 1U) {
    if (n - *at < 8U) return WL_CODEC_ERR_MALFORMED;
    *at += 8U;
    return WL_CODEC_OK;
  }
  if (wire == 5U) {
    if (n - *at < 4U) return WL_CODEC_ERR_MALFORMED;
    *at += 4U;
    return WL_CODEC_OK;
  }
  if (wire != 2U) return WL_CODEC_ERR_MALFORMED;
  if ((s = device_rpc_wlc_getv(in, n, at, &length)) != WL_CODEC_OK) return s;
  if (length > n - *at) return WL_CODEC_ERR_MALFORMED;
  *at += (size_t)length;
  return WL_CODEC_OK;
}
static wl_codec_status_t device_rpc_wlc_read_fixed(uint8_t kind, const uint8_t *in,
                                        size_t n, size_t *at, void *out) {
  size_t bytes = (kind == DEVICE_RPC_WLC_F32 || kind == DEVICE_RPC_WLC_FLOAT32) ? 4U : 8U;
  if (n - *at < bytes) return WL_CODEC_ERR_MALFORMED;
  uint64_t bits = 0U;
  for (size_t i = 0U; i < bytes; ++i) bits = (bits << 8U) | in[(*at)++];
  switch (kind) {
    case DEVICE_RPC_WLC_F64: *(uint64_t *)out = bits; break;
    case DEVICE_RPC_WLC_FLOAT32: {
      uint32_t bits32 = (uint32_t)bits;
      memcpy(out, &bits32, sizeof(bits32)); break;
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
  return WL_CODEC_OK;
}
static wl_codec_status_t device_rpc_wlc_read_value(const device_rpc_wlc_field_t *f, const uint8_t *in,
                                        size_t n, size_t *at, void *out) {
  (void)in; (void)n; (void)at; (void)out;
  switch (f->kind) {
    case DEVICE_RPC_WLC_F64:
    case DEVICE_RPC_WLC_FLOAT32:
      return device_rpc_wlc_read_fixed(f->kind, in, n, at, out);
    case DEVICE_RPC_WLC_BYTES:
    case DEVICE_RPC_WLC_STRING:
    case DEVICE_RPC_WLC_MESSAGE:
      {
      uint64_t v;
      wl_codec_status_t s = device_rpc_wlc_getv(in, n, at, &v);
      if (s != WL_CODEC_OK) return s;
      if ((f->kind == DEVICE_RPC_WLC_BYTES || f->kind == DEVICE_RPC_WLC_STRING) &&
          f->max_length != 0U && v > (uint64_t)f->max_length)
        return WL_CODEC_ERR_INVALID_VALUE;
      if (v > n - *at) return WL_CODEC_ERR_MALFORMED;
      size_t bytes = (size_t)v;
      if (f->kind == DEVICE_RPC_WLC_BYTES)
        *(wl_codec_bytes_t *)out = (wl_codec_bytes_t){in + *at, bytes};
      if (f->kind == DEVICE_RPC_WLC_STRING) {
        if (!device_rpc_wlc_utf8(in + *at, bytes)) return WL_CODEC_ERR_UTF8;
        *(wl_codec_string_t *)out = (wl_codec_string_t){(const char *)(in + *at), bytes};
      }
      if (f->kind == DEVICE_RPC_WLC_MESSAGE) {
        s = device_rpc_wlc_decode(f->nested, in + *at, bytes, out);
        if (s != WL_CODEC_OK) return s;
      }
      *at += bytes;
      return WL_CODEC_OK;
    }
    default: break;
  }
  uint64_t v;
  wl_codec_status_t s = device_rpc_wlc_getv(in, n, at, &v);
  if (s != WL_CODEC_OK) return s;
  switch (f->kind) {
    case DEVICE_RPC_WLC_BOOL:
      if (v > 1U) return WL_CODEC_ERR_INVALID_VALUE;
      *(bool *)out = v != 0U; return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U16:
      if (v > UINT16_MAX) return WL_CODEC_ERR_OVERFLOW;
      *(uint16_t *)out = (uint16_t)v; return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U32:
      if (v > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
      *(uint32_t *)out = (uint32_t)v; return WL_CODEC_OK;
    case DEVICE_RPC_WLC_U64: *(uint64_t *)out = v; return WL_CODEC_OK;
    case DEVICE_RPC_WLC_ENUM:

      if (v > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
      *(int32_t *)out = device_rpc_wlc_uz32((uint32_t)v); return WL_CODEC_OK;
    case DEVICE_RPC_WLC_I64: *(int64_t *)out = device_rpc_wlc_uz64(v); return WL_CODEC_OK;
    default: break;
  }
  return WL_CODEC_ERR_INVALID_VALUE;
}
static wl_codec_status_t device_rpc_wlc_read_packed(const device_rpc_wlc_field_t *f,
                                         const uint8_t *in, size_t n,
                                         size_t *at, void *out) {
  uint64_t encoded_bytes;
  size_t expected_bytes;
  wl_codec_status_t s;
  if ((s = device_rpc_wlc_getv(in, n, at, &encoded_bytes)) != WL_CODEC_OK) return s;
  if ((s = device_rpc_wlc_packed_bytes(f, &expected_bytes)) != WL_CODEC_OK) return s;
  if (encoded_bytes != expected_bytes || expected_bytes > n - *at)
    return WL_CODEC_ERR_MALFORMED;
  if (f->kind == DEVICE_RPC_WLC_F32 || f->kind == DEVICE_RPC_WLC_FLOAT32) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint32_t bits = ((uint32_t)in[*at] << 24U) |
                      ((uint32_t)in[*at + 1U] << 16U) |
                      ((uint32_t)in[*at + 2U] << 8U) |
                      (uint32_t)in[*at + 3U];
      memcpy((uint8_t *)out + j * f->element, &bits, sizeof(bits));
      *at += 4U;
    }
    return WL_CODEC_OK;
  }
  if (f->kind == DEVICE_RPC_WLC_F64 || f->kind == DEVICE_RPC_WLC_FLOAT64) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint64_t bits = 0U;
      for (size_t i = 0U; i < 8U; ++i) bits = (bits << 8U) | in[(*at)++];
      memcpy((uint8_t *)out + j * f->element, &bits, sizeof(bits));
    }
    return WL_CODEC_OK;
  }
  return WL_CODEC_ERR_INVALID_VALUE;
}
static const device_rpc_wlc_field_t *device_rpc_wlc_find_field(const device_rpc_wlc_desc_t *d, uint16_t number) {
  if (d->lookup == DEVICE_RPC_WLC_LOOKUP_DENSE) {
    size_t index = (size_t)number - (size_t)d->fields[0].number;
    return index < d->count ? &d->fields[index] : NULL;
  }
  if (d->lookup == DEVICE_RPC_WLC_LOOKUP_BINARY) {
    size_t lo = 0U, hi = d->count;
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2U;
      uint16_t candidate = d->fields[mid].number;
      if (candidate == number) return &d->fields[mid];
      if (candidate < number) lo = mid + 1U;
      else hi = mid;
    }
    return NULL;
  }
  for (size_t i = 0U; i < d->count; ++i)
    if (d->fields[i].number == number) return &d->fields[i];
  return NULL;
}
static inline wl_codec_status_t device_rpc_wlc_decode(const device_rpc_wlc_desc_t *d, const uint8_t *in,
                                    size_t n, void *out) {
  if (d == NULL || out == NULL || (n != 0U && in == NULL))
    return WL_CODEC_ERR_INVALID_VALUE;
  device_rpc_wlc_clear(d, out);
  // A hint only: missing, unknown or reordered fields still use exact lookup.
  // Each recursive decode owns its cursor; empty schemas avoid NULL arithmetic.
  const device_rpc_wlc_field_t *next = d->fields;
  const device_rpc_wlc_field_t *end = d->count == 0U ? next : next + d->count;
  for (size_t at = 0U; at < n;) {
    uint64_t key;
    wl_codec_status_t s = device_rpc_wlc_getv(in, n, &at, &key);
    if (s != WL_CODEC_OK) return s;
    uint64_t raw_number = key >> 3U;
    uint8_t wire = (uint8_t)(key & 7U);
    if (raw_number == 0U || raw_number > 65535U ||
        (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    uint16_t number = (uint16_t)raw_number;
    const device_rpc_wlc_field_t *f = next != end && next->number == number
                               ? next : device_rpc_wlc_find_field(d, number);
    if (f == NULL) {
      if ((s = device_rpc_wlc_skip(wire, in, n, &at)) != WL_CODEC_OK) return s;
      continue;
    }
    if (wire != device_rpc_wlc_wire(f)) return WL_CODEC_ERR_WIRE_TYPE;
    next = f->card == DEVICE_RPC_WLC_REPEATED ? f : f + 1;
    uint8_t *base = out;
    if (f->card == DEVICE_RPC_WLC_PACKED) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
      if ((s = device_rpc_wlc_read_packed(f, in, n, &at, base + f->value)) != WL_CODEC_OK)
        return s;
      *(bool *)(base + f->has) = true;
      continue;
    }
    void *p = base + f->value;
    if (f->card == DEVICE_RPC_WLC_OPTIONAL) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
    }
    if ((s = device_rpc_wlc_read_value(f, in, n, &at, p)) != WL_CODEC_OK) return s;
    if (f->card == DEVICE_RPC_WLC_OPTIONAL) *(bool *)(base + f->has) = true;
  }
  for (size_t i = 0U; i < d->count; ++i) {
    const device_rpc_wlc_field_t *f = &d->fields[i];
    if (f->required != 0U && !*(const bool *)((uint8_t *)out + f->has))
      return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  }
  return WL_CODEC_OK;
}
static const device_rpc_wlc_desc_t device_rpc_info_request_desc = { NULL, 0U, DEVICE_RPC_WLC_LOOKUP_LINEAR };

static const device_rpc_wlc_field_t device_rpc_settings_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 0, 2U, 1U, offsetof(settings_t, label), offsetof(settings_t, has_label), 0, 0, sizeof(wl_codec_string_t), 0U, 12U, { 10U, 0U, 0U }, 0, 6ULL, "\xE8""\xAE""\xBE""\xE5""\xA4""\x87", NULL },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BOOL, 1, 0U, 1U, offsetof(settings_t, enabled), offsetof(settings_t, has_enabled), 0, 0, sizeof(bool), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_ENUM, 0, 0U, 1U, offsetof(settings_t, mode), offsetof(settings_t, has_mode), 0, 0, sizeof(mode_t), 0U, 0U, { 24U, 0U, 0U }, INT32_C(1), 0ULL, NULL, NULL },
  { 4U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 1, 2U, 1U, offsetof(settings_t, token), offsetof(settings_t, has_token), 0, 0, sizeof(wl_codec_bytes_t), 0U, 8U, { 34U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 5U, DEVICE_RPC_WLC_PACKED, DEVICE_RPC_WLC_FLOAT32, 1, 2U, 1U, offsetof(settings_t, gains), offsetof(settings_t, has_gains), 0, 0, sizeof(float), 3U, 0U, { 42U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 6U, DEVICE_RPC_WLC_PACKED, DEVICE_RPC_WLC_F64, 0, 2U, 1U, offsetof(settings_t, counters), offsetof(settings_t, has_counters), 0, 0, sizeof(uint64_t), 2U, 0U, { 50U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 7U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_I64, 0, 0U, 1U, offsetof(settings_t, floor), offsetof(settings_t, has_floor), 0, 0, sizeof(int64_t), 0U, 0U, { 56U, 0U, 0U }, INT64_MIN, 0ULL, NULL, NULL },
  { 8U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U64, 0, 0U, 1U, offsetof(settings_t, ceiling), offsetof(settings_t, has_ceiling), 0, 0, sizeof(uint64_t), 0U, 0U, { 64U, 0U, 0U }, 0, 18446744073709551615ULL, NULL, NULL },
  { 9U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 0, 2U, 1U, offsetof(settings_t, class_), offsetof(settings_t, has_class_), 0, 0, sizeof(wl_codec_string_t), 0U, 8U, { 74U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 10U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U16, 0, 0U, 1U, offsetof(settings_t, timeout), offsetof(settings_t, has_timeout), 0, 0, sizeof(uint16_t), 0U, 0U, { 80U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const device_rpc_wlc_desc_t device_rpc_settings_desc = { device_rpc_settings_fields, sizeof(device_rpc_settings_fields) / sizeof(device_rpc_settings_fields[0]), DEVICE_RPC_WLC_LOOKUP_DENSE };

static const device_rpc_wlc_field_t device_rpc_info_response_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 1, 2U, 1U, offsetof(info_response_t, name), offsetof(info_response_t, has_name), 0, 0, sizeof(wl_codec_string_t), 0U, 32U, { 10U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(info_response_t, settings), offsetof(info_response_t, has_settings), 0, 0, sizeof(settings_t), 0U, 0U, { 18U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_desc },
};
static const device_rpc_wlc_desc_t device_rpc_info_response_desc = { device_rpc_info_response_fields, sizeof(device_rpc_info_response_fields) / sizeof(device_rpc_info_response_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR };

static const device_rpc_wlc_field_t device_rpc_configure_request_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(configure_request_t, settings), offsetof(configure_request_t, has_settings), 0, 0, sizeof(settings_t), 0U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_desc },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 0, 2U, 1U, offsetof(configure_request_t, opaque), offsetof(configure_request_t, has_opaque), 0, 0, sizeof(wl_codec_bytes_t), 0U, 4U, { 18U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 0, 2U, 1U, offsetof(configure_request_t, backup), offsetof(configure_request_t, has_backup), 0, 0, sizeof(settings_t), 0U, 0U, { 26U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_desc },
};
static const device_rpc_wlc_desc_t device_rpc_configure_request_desc = { device_rpc_configure_request_fields, sizeof(device_rpc_configure_request_fields) / sizeof(device_rpc_configure_request_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR };

static const device_rpc_wlc_field_t device_rpc_configure_response_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(configure_response_t, settings), offsetof(configure_response_t, has_settings), 0, 0, sizeof(settings_t), 0U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_desc },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 0, 2U, 1U, offsetof(configure_response_t, opaque), offsetof(configure_response_t, has_opaque), 0, 0, sizeof(wl_codec_bytes_t), 0U, 4U, { 18U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 0, 2U, 1U, offsetof(configure_response_t, backup), offsetof(configure_response_t, has_backup), 0, 0, sizeof(settings_t), 0U, 0U, { 26U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_desc },
  { 4U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U32, 1, 0U, 1U, offsetof(configure_response_t, count), offsetof(configure_response_t, has_count), 0, 0, sizeof(uint32_t), 0U, 0U, { 32U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const device_rpc_wlc_desc_t device_rpc_configure_response_desc = { device_rpc_configure_response_fields, sizeof(device_rpc_configure_response_fields) / sizeof(device_rpc_configure_response_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR };

static inline wl_codec_status_t device_rpc_info_request_wlc_detail_measure(const info_request_t *value, size_t *size) { return device_rpc_wlc_measure(&device_rpc_info_request_desc, value, size); }
static inline void device_rpc_info_request_clear(info_request_t *value) { if (value != NULL) device_rpc_wlc_clear(&device_rpc_info_request_desc, value); }
static inline size_t device_rpc_info_request_encoded_size(const info_request_t *value) { size_t size; return device_rpc_wlc_measure(&device_rpc_info_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
static inline wl_codec_status_t device_rpc_info_request_encode(const info_request_t *value, uint8_t *out, size_t cap, size_t *length) { return device_rpc_wlc_encode(&device_rpc_info_request_desc, value, out, cap, length); }
static inline wl_codec_status_t device_rpc_info_request_decode(const uint8_t *input, size_t length, info_request_t *out) { return device_rpc_wlc_decode(&device_rpc_info_request_desc, input, length, out); }

static inline wl_codec_status_t device_rpc_settings_wlc_detail_measure(const settings_t *value, size_t *size) { return device_rpc_wlc_measure(&device_rpc_settings_desc, value, size); }
static inline void device_rpc_settings_clear(settings_t *value) { if (value != NULL) device_rpc_wlc_clear(&device_rpc_settings_desc, value); }
static inline size_t device_rpc_settings_encoded_size(const settings_t *value) { size_t size; return device_rpc_wlc_measure(&device_rpc_settings_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
static inline wl_codec_status_t device_rpc_settings_encode(const settings_t *value, uint8_t *out, size_t cap, size_t *length) { return device_rpc_wlc_encode(&device_rpc_settings_desc, value, out, cap, length); }
static inline wl_codec_status_t device_rpc_settings_decode(const uint8_t *input, size_t length, settings_t *out) { return device_rpc_wlc_decode(&device_rpc_settings_desc, input, length, out); }

static inline wl_codec_status_t device_rpc_info_response_wlc_detail_measure(const info_response_t *value, size_t *size) { return device_rpc_wlc_measure(&device_rpc_info_response_desc, value, size); }
static inline void device_rpc_info_response_clear(info_response_t *value) { if (value != NULL) device_rpc_wlc_clear(&device_rpc_info_response_desc, value); }
static inline size_t device_rpc_info_response_encoded_size(const info_response_t *value) { size_t size; return device_rpc_wlc_measure(&device_rpc_info_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
static inline wl_codec_status_t device_rpc_info_response_encode(const info_response_t *value, uint8_t *out, size_t cap, size_t *length) { return device_rpc_wlc_encode(&device_rpc_info_response_desc, value, out, cap, length); }
static inline wl_codec_status_t device_rpc_info_response_decode(const uint8_t *input, size_t length, info_response_t *out) { return device_rpc_wlc_decode(&device_rpc_info_response_desc, input, length, out); }

static inline wl_codec_status_t device_rpc_configure_request_wlc_detail_measure(const configure_request_t *value, size_t *size) { return device_rpc_wlc_measure(&device_rpc_configure_request_desc, value, size); }
static inline void device_rpc_configure_request_clear(configure_request_t *value) { if (value != NULL) device_rpc_wlc_clear(&device_rpc_configure_request_desc, value); }
static inline size_t device_rpc_configure_request_encoded_size(const configure_request_t *value) { size_t size; return device_rpc_wlc_measure(&device_rpc_configure_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
static inline wl_codec_status_t device_rpc_configure_request_encode(const configure_request_t *value, uint8_t *out, size_t cap, size_t *length) { return device_rpc_wlc_encode(&device_rpc_configure_request_desc, value, out, cap, length); }
static inline wl_codec_status_t device_rpc_configure_request_decode(const uint8_t *input, size_t length, configure_request_t *out) { return device_rpc_wlc_decode(&device_rpc_configure_request_desc, input, length, out); }

static inline wl_codec_status_t device_rpc_configure_response_wlc_detail_measure(const configure_response_t *value, size_t *size) { return device_rpc_wlc_measure(&device_rpc_configure_response_desc, value, size); }
static inline void device_rpc_configure_response_clear(configure_response_t *value) { if (value != NULL) device_rpc_wlc_clear(&device_rpc_configure_response_desc, value); }
static inline size_t device_rpc_configure_response_encoded_size(const configure_response_t *value) { size_t size; return device_rpc_wlc_measure(&device_rpc_configure_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
static inline wl_codec_status_t device_rpc_configure_response_encode(const configure_response_t *value, uint8_t *out, size_t cap, size_t *length) { return device_rpc_wlc_encode(&device_rpc_configure_response_desc, value, out, cap, length); }
static inline wl_codec_status_t device_rpc_configure_response_decode(const uint8_t *input, size_t length, configure_response_t *out) { return device_rpc_wlc_decode(&device_rpc_configure_response_desc, input, length, out); }

/* A descriptor embeds the ordinary wire plan as its first member. Owned
 * lengths/data use actual member offsets; no view struct aliases an owned one. */
typedef struct {
  device_rpc_wlc_desc_t wire;
  size_t size;
  void (*defaults)(void *);
} device_rpc_wlc_value_desc_t;
static inline wl_codec_status_t device_rpc_wlc_value_decode(const device_rpc_wlc_desc_t *, const uint8_t *, size_t, void *);
static void device_rpc_wlc_value_clear(const device_rpc_wlc_desc_t *wire, void *out) {
  const device_rpc_wlc_value_desc_t *d = (const device_rpc_wlc_value_desc_t *)wire;
  memset(out, 0, d->size);
  d->defaults(out);
}
static wl_codec_status_t device_rpc_wlc_value_read(const device_rpc_wlc_field_t *f, const uint8_t *in,
    size_t n, size_t *at, void *out) {
  if (f->kind == DEVICE_RPC_WLC_BYTES || f->kind == DEVICE_RPC_WLC_STRING || f->kind == DEVICE_RPC_WLC_MESSAGE) {
    uint64_t length;
    wl_codec_status_t status = device_rpc_wlc_getv(in, n, at, &length);
    if (status != WL_CODEC_OK) return status;
    if ((f->kind == DEVICE_RPC_WLC_BYTES || f->kind == DEVICE_RPC_WLC_STRING) &&
        f->max_length != 0U && length > f->max_length) return WL_CODEC_ERR_INVALID_VALUE;
    if (length > n - *at) return WL_CODEC_ERR_MALFORMED;
    size_t bytes = (size_t)length;
    if (f->kind == DEVICE_RPC_WLC_MESSAGE) {
      status = device_rpc_wlc_value_decode(f->nested, in + *at, bytes, out);
      if (status != WL_CODEC_OK) return status;
    } else
    {
      if (f->kind == DEVICE_RPC_WLC_STRING && !device_rpc_wlc_utf8(in + *at, bytes)) return WL_CODEC_ERR_UTF8;
      uint8_t *data = (uint8_t *)out + f->capacity;
      *(size_t *)out = bytes;
      if (f->string_default != NULL) memset(data, 0, (size_t)f->max_length + 1U);
      if (bytes != 0U) memcpy(data, in + *at, bytes);
      if (f->kind == DEVICE_RPC_WLC_STRING) data[bytes] = 0U;
    }
    *at += bytes;
    return WL_CODEC_OK;
  }
  return device_rpc_wlc_read_value(f, in, n, at, out);
}
static inline wl_codec_status_t device_rpc_wlc_value_decode(const device_rpc_wlc_desc_t *d, const uint8_t *in,
                                    size_t n, void *out) {
  if (d == NULL || out == NULL || (n != 0U && in == NULL))
    return WL_CODEC_ERR_INVALID_VALUE;
  device_rpc_wlc_value_clear(d, out);
  // A hint only: missing, unknown or reordered fields still use exact lookup.
  // Each recursive decode owns its cursor; empty schemas avoid NULL arithmetic.
  const device_rpc_wlc_field_t *next = d->fields;
  const device_rpc_wlc_field_t *end = d->count == 0U ? next : next + d->count;
  for (size_t at = 0U; at < n;) {
    uint64_t key;
    wl_codec_status_t s = device_rpc_wlc_getv(in, n, &at, &key);
    if (s != WL_CODEC_OK) return s;
    uint64_t raw_number = key >> 3U;
    uint8_t wire = (uint8_t)(key & 7U);
    if (raw_number == 0U || raw_number > 65535U ||
        (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    uint16_t number = (uint16_t)raw_number;
    const device_rpc_wlc_field_t *f = next != end && next->number == number
                               ? next : device_rpc_wlc_find_field(d, number);
    if (f == NULL) {
      if ((s = device_rpc_wlc_skip(wire, in, n, &at)) != WL_CODEC_OK) return s;
      continue;
    }
    if (wire != device_rpc_wlc_wire(f)) return WL_CODEC_ERR_WIRE_TYPE;
    next = f->card == DEVICE_RPC_WLC_REPEATED ? f : f + 1;
    uint8_t *base = out;
    if (f->card == DEVICE_RPC_WLC_PACKED) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
      if ((s = device_rpc_wlc_read_packed(f, in, n, &at, base + f->value)) != WL_CODEC_OK)
        return s;
      *(bool *)(base + f->has) = true;
      continue;
    }
    void *p = base + f->value;
    if (f->card == DEVICE_RPC_WLC_OPTIONAL) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
    }
    if ((s = device_rpc_wlc_value_read(f, in, n, &at, p)) != WL_CODEC_OK) return s;
    if (f->card == DEVICE_RPC_WLC_OPTIONAL) *(bool *)(base + f->has) = true;
  }
  for (size_t i = 0U; i < d->count; ++i) {
    const device_rpc_wlc_field_t *f = &d->fields[i];
    if (f->required != 0U && !*(const bool *)((uint8_t *)out + f->has))
      return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  }
  return WL_CODEC_OK;
}
static void device_rpc_info_request_value_defaults(info_request_value_t *out) {
  (void)out;
}

static void device_rpc_info_request_value_decode_defaults(void *out) { device_rpc_info_request_value_defaults(out); }

static const device_rpc_wlc_value_desc_t device_rpc_info_request_value_desc = { { NULL, 0U, DEVICE_RPC_WLC_LOOKUP_LINEAR }, sizeof(info_request_value_t), device_rpc_info_request_value_decode_defaults };

static wl_codec_status_t device_rpc_info_request_value_borrow(const info_request_value_t *, info_request_t *);
static inline wl_codec_status_t device_rpc_info_request_wlc_detail_value_decode(const uint8_t *input, size_t length, info_request_value_t *out) { return device_rpc_wlc_value_decode(&device_rpc_info_request_value_desc.wire, input, length, out); }

static void device_rpc_info_request_value_copy_fields(const info_request_t *view, info_request_value_t *out) {
  (void)view; (void)out;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
static inline void device_rpc_info_request_wlc_detail_value_copy(const info_request_t *view, info_request_value_t *out) {
  memset(out, 0, sizeof(*out));
  device_rpc_info_request_value_copy_fields(view, out);
}

static inline void device_rpc_info_request_value_clear(info_request_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  device_rpc_info_request_value_defaults(value);
}

static inline wl_codec_status_t device_rpc_info_request_value_from_view(const info_request_t *view, info_request_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_request_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  device_rpc_info_request_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t device_rpc_info_request_value_borrow(const info_request_value_t *value, info_request_t *out) {
  device_rpc_info_request_clear(out);
  (void)value;
  out->_empty = 0U;
  return WL_CODEC_OK;
}

static inline wl_codec_status_t device_rpc_info_request_value_to_view(const info_request_value_t *value, info_request_t *out) {
  info_request_t view = {0};
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_request_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = device_rpc_info_request_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

static inline size_t device_rpc_info_request_value_encoded_size(const info_request_value_t *value) {
  info_request_t view = {0};
  if (value == NULL || device_rpc_info_request_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return device_rpc_info_request_encoded_size(&view);
}

static inline wl_codec_status_t device_rpc_info_request_value_encode(const info_request_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  info_request_t view = {0};
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_request_value_borrow(value, &view);
  return status == WL_CODEC_OK ? device_rpc_info_request_encode(&view, out, capacity, length) : status;
}

static inline wl_codec_status_t device_rpc_info_request_value_decode(const uint8_t *input, size_t length, info_request_value_t *out) {
  info_request_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_request_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

static void device_rpc_settings_value_defaults(settings_value_t *out) {
  (void)out;
  out->label.length = 6U;
  memcpy(out->label.data, "\xE8""\xAE""\xBE""\xE5""\xA4""\x87", 6U);
  out->enabled = 0;
  out->mode = INT32_C(1);
  out->floor = INT64_MIN;
  out->ceiling = UINT64_C(18446744073709551615);
  out->timeout = 0;
}

static void device_rpc_settings_value_decode_defaults(void *out) { device_rpc_settings_value_defaults(out); }

static const device_rpc_wlc_field_t device_rpc_settings_value_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 0, 2U, 1U, offsetof(settings_value_t, label), offsetof(settings_value_t, has_label), 0, offsetof(settings_value_t, label.data) - offsetof(settings_value_t, label), sizeof(wl_codec_string_t), 0U, 12U, { 10U, 0U, 0U }, 0, 6ULL, "\xE8""\xAE""\xBE""\xE5""\xA4""\x87", NULL },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BOOL, 1, 0U, 1U, offsetof(settings_value_t, enabled), offsetof(settings_value_t, has_enabled), 0, 0, sizeof(bool), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_ENUM, 0, 0U, 1U, offsetof(settings_value_t, mode), offsetof(settings_value_t, has_mode), 0, 0, sizeof(mode_t), 0U, 0U, { 24U, 0U, 0U }, INT32_C(1), 0ULL, NULL, NULL },
  { 4U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 1, 2U, 1U, offsetof(settings_value_t, token), offsetof(settings_value_t, has_token), 0, offsetof(settings_value_t, token.data) - offsetof(settings_value_t, token), sizeof(wl_codec_bytes_t), 0U, 8U, { 34U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 5U, DEVICE_RPC_WLC_PACKED, DEVICE_RPC_WLC_FLOAT32, 1, 2U, 1U, offsetof(settings_value_t, gains), offsetof(settings_value_t, has_gains), 0, 0, sizeof(float), 3U, 0U, { 42U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 6U, DEVICE_RPC_WLC_PACKED, DEVICE_RPC_WLC_F64, 0, 2U, 1U, offsetof(settings_value_t, counters), offsetof(settings_value_t, has_counters), 0, 0, sizeof(uint64_t), 2U, 0U, { 50U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 7U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_I64, 0, 0U, 1U, offsetof(settings_value_t, floor), offsetof(settings_value_t, has_floor), 0, 0, sizeof(int64_t), 0U, 0U, { 56U, 0U, 0U }, INT64_MIN, 0ULL, NULL, NULL },
  { 8U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U64, 0, 0U, 1U, offsetof(settings_value_t, ceiling), offsetof(settings_value_t, has_ceiling), 0, 0, sizeof(uint64_t), 0U, 0U, { 64U, 0U, 0U }, 0, 18446744073709551615ULL, NULL, NULL },
  { 9U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 0, 2U, 1U, offsetof(settings_value_t, class_), offsetof(settings_value_t, has_class_), 0, offsetof(settings_value_t, class_.data) - offsetof(settings_value_t, class_), sizeof(wl_codec_string_t), 0U, 8U, { 74U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 10U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U16, 0, 0U, 1U, offsetof(settings_value_t, timeout), offsetof(settings_value_t, has_timeout), 0, 0, sizeof(uint16_t), 0U, 0U, { 80U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const device_rpc_wlc_value_desc_t device_rpc_settings_value_desc = { { device_rpc_settings_value_fields, sizeof(device_rpc_settings_value_fields) / sizeof(device_rpc_settings_value_fields[0]), DEVICE_RPC_WLC_LOOKUP_DENSE }, sizeof(settings_value_t), device_rpc_settings_value_decode_defaults };

static wl_codec_status_t device_rpc_settings_value_borrow(const settings_value_t *, settings_t *);
static inline wl_codec_status_t device_rpc_settings_wlc_detail_value_decode(const uint8_t *input, size_t length, settings_value_t *out) { return device_rpc_wlc_value_decode(&device_rpc_settings_value_desc.wire, input, length, out); }

static void device_rpc_settings_value_copy_fields(const settings_t *view, settings_value_t *out) {
  out->has_label = view->has_label;
  if (view->has_label) {
    out->label.length = view->label.length;
    if (view->label.length != 0U) memcpy(out->label.data, view->label.data, view->label.length);
  } else {
  out->label.length = 6U;
  memcpy(out->label.data, "\xE8""\xAE""\xBE""\xE5""\xA4""\x87", 6U);
  }
  out->has_enabled = view->has_enabled;
  out->enabled = view->has_enabled ? view->enabled : 0;
  out->has_mode = view->has_mode;
  out->mode = view->has_mode ? view->mode : INT32_C(1);
  out->has_token = view->has_token;
  if (view->has_token) {
    out->token.length = view->token.length;
    if (view->token.length != 0U) memcpy(out->token.data, view->token.data, view->token.length);
  } else {
  }
  out->has_gains = view->has_gains;
  if (view->has_gains) memcpy(out->gains, view->gains, sizeof(out->gains));
  out->has_counters = view->has_counters;
  if (view->has_counters) memcpy(out->counters, view->counters, sizeof(out->counters));
  out->has_floor = view->has_floor;
  out->floor = view->has_floor ? view->floor : INT64_MIN;
  out->has_ceiling = view->has_ceiling;
  out->ceiling = view->has_ceiling ? view->ceiling : UINT64_C(18446744073709551615);
  out->has_class_ = view->has_class_;
  if (view->has_class_) {
    out->class_.length = view->class_.length;
    if (view->class_.length != 0U) memcpy(out->class_.data, view->class_.data, view->class_.length);
  } else {
  }
  out->has_timeout = view->has_timeout;
  out->timeout = view->has_timeout ? view->timeout : 0;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
static inline void device_rpc_settings_wlc_detail_value_copy(const settings_t *view, settings_value_t *out) {
  memset(out, 0, sizeof(*out));
  device_rpc_settings_value_copy_fields(view, out);
}

static inline void device_rpc_settings_value_clear(settings_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  device_rpc_settings_value_defaults(value);
}

static inline wl_codec_status_t device_rpc_settings_value_from_view(const settings_t *view, settings_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_settings_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  device_rpc_settings_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t device_rpc_settings_value_borrow(const settings_value_t *value, settings_t *out) {
  device_rpc_settings_clear(out);
  out->has_label = value->has_label;
  if (value->has_label) {
    if (value->label.length > 12U) return WL_CODEC_ERR_INVALID_VALUE;
    out->label.length = value->label.length;
    out->label.data = value->label.data;
  }
  out->has_enabled = value->has_enabled;
  if (value->has_enabled) {
    out->enabled = value->enabled;
  }
  out->has_mode = value->has_mode;
  if (value->has_mode) {
    out->mode = value->mode;
  }
  out->has_token = value->has_token;
  if (value->has_token) {
    if (value->token.length > 8U) return WL_CODEC_ERR_INVALID_VALUE;
    out->token.length = value->token.length;
    out->token.data = value->token.data;
  }
  out->has_gains = value->has_gains;
  if (value->has_gains) {
    memcpy(out->gains, value->gains, sizeof(out->gains));
  }
  out->has_counters = value->has_counters;
  if (value->has_counters) {
    memcpy(out->counters, value->counters, sizeof(out->counters));
  }
  out->has_floor = value->has_floor;
  if (value->has_floor) {
    out->floor = value->floor;
  }
  out->has_ceiling = value->has_ceiling;
  if (value->has_ceiling) {
    out->ceiling = value->ceiling;
  }
  out->has_class_ = value->has_class_;
  if (value->has_class_) {
    if (value->class_.length > 8U) return WL_CODEC_ERR_INVALID_VALUE;
    out->class_.length = value->class_.length;
    out->class_.data = value->class_.data;
  }
  out->has_timeout = value->has_timeout;
  if (value->has_timeout) {
    out->timeout = value->timeout;
  }
  return WL_CODEC_OK;
}

static inline wl_codec_status_t device_rpc_settings_value_to_view(const settings_value_t *value, settings_t *out) {
  settings_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_settings_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = device_rpc_settings_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

static inline size_t device_rpc_settings_value_encoded_size(const settings_value_t *value) {
  settings_t view;
  if (value == NULL || device_rpc_settings_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return device_rpc_settings_encoded_size(&view);
}

static inline wl_codec_status_t device_rpc_settings_value_encode(const settings_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  settings_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_settings_value_borrow(value, &view);
  return status == WL_CODEC_OK ? device_rpc_settings_encode(&view, out, capacity, length) : status;
}

static inline wl_codec_status_t device_rpc_settings_value_decode(const uint8_t *input, size_t length, settings_value_t *out) {
  settings_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_settings_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

static void device_rpc_info_response_value_defaults(info_response_value_t *out) {
  (void)out;
  device_rpc_settings_value_defaults(&out->settings);
}

static void device_rpc_info_response_value_decode_defaults(void *out) { device_rpc_info_response_value_defaults(out); }

static const device_rpc_wlc_field_t device_rpc_info_response_value_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_STRING, 1, 2U, 1U, offsetof(info_response_value_t, name), offsetof(info_response_value_t, has_name), 0, offsetof(info_response_value_t, name.data) - offsetof(info_response_value_t, name), sizeof(wl_codec_string_t), 0U, 32U, { 10U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(info_response_value_t, settings), offsetof(info_response_value_t, has_settings), 0, 0, sizeof(settings_value_t), 0U, 0U, { 18U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_value_desc.wire },
};
static const device_rpc_wlc_value_desc_t device_rpc_info_response_value_desc = { { device_rpc_info_response_value_fields, sizeof(device_rpc_info_response_value_fields) / sizeof(device_rpc_info_response_value_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR }, sizeof(info_response_value_t), device_rpc_info_response_value_decode_defaults };

static wl_codec_status_t device_rpc_info_response_value_borrow(const info_response_value_t *, info_response_t *);
static inline wl_codec_status_t device_rpc_info_response_wlc_detail_value_decode(const uint8_t *input, size_t length, info_response_value_t *out) { return device_rpc_wlc_value_decode(&device_rpc_info_response_value_desc.wire, input, length, out); }

static void device_rpc_info_response_value_copy_fields(const info_response_t *view, info_response_value_t *out) {
  out->has_name = view->has_name;
  if (view->has_name) {
    out->name.length = view->name.length;
    if (view->name.length != 0U) memcpy(out->name.data, view->name.data, view->name.length);
  } else {
  }
  out->has_settings = view->has_settings;
  if (view->has_settings) device_rpc_settings_value_copy_fields(&view->settings, &out->settings);
  else device_rpc_settings_value_defaults(&out->settings);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
static inline void device_rpc_info_response_wlc_detail_value_copy(const info_response_t *view, info_response_value_t *out) {
  memset(out, 0, sizeof(*out));
  device_rpc_info_response_value_copy_fields(view, out);
}

static inline void device_rpc_info_response_value_clear(info_response_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  device_rpc_info_response_value_defaults(value);
}

static inline wl_codec_status_t device_rpc_info_response_value_from_view(const info_response_t *view, info_response_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_response_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  device_rpc_info_response_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t device_rpc_info_response_value_borrow(const info_response_value_t *value, info_response_t *out) {
  device_rpc_info_response_clear(out);
  out->has_name = value->has_name;
  if (value->has_name) {
    if (value->name.length > 32U) return WL_CODEC_ERR_INVALID_VALUE;
    out->name.length = value->name.length;
    out->name.data = value->name.data;
  }
  out->has_settings = value->has_settings;
  if (value->has_settings) {
    wl_codec_status_t status = device_rpc_settings_value_borrow(&value->settings, &out->settings);
    if (status != WL_CODEC_OK) return status;
  }
  return WL_CODEC_OK;
}

static inline wl_codec_status_t device_rpc_info_response_value_to_view(const info_response_value_t *value, info_response_t *out) {
  info_response_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_response_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = device_rpc_info_response_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

static inline size_t device_rpc_info_response_value_encoded_size(const info_response_value_t *value) {
  info_response_t view;
  if (value == NULL || device_rpc_info_response_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return device_rpc_info_response_encoded_size(&view);
}

static inline wl_codec_status_t device_rpc_info_response_value_encode(const info_response_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  info_response_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_response_value_borrow(value, &view);
  return status == WL_CODEC_OK ? device_rpc_info_response_encode(&view, out, capacity, length) : status;
}

static inline wl_codec_status_t device_rpc_info_response_value_decode(const uint8_t *input, size_t length, info_response_value_t *out) {
  info_response_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_info_response_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

static void device_rpc_configure_request_value_defaults(configure_request_value_t *out) {
  (void)out;
  device_rpc_settings_value_defaults(&out->settings);
  device_rpc_settings_value_defaults(&out->backup);
}

static void device_rpc_configure_request_value_decode_defaults(void *out) { device_rpc_configure_request_value_defaults(out); }

static const device_rpc_wlc_field_t device_rpc_configure_request_value_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(configure_request_value_t, settings), offsetof(configure_request_value_t, has_settings), 0, 0, sizeof(settings_value_t), 0U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_value_desc.wire },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 0, 2U, 1U, offsetof(configure_request_value_t, opaque), offsetof(configure_request_value_t, has_opaque), 0, offsetof(configure_request_value_t, opaque.data) - offsetof(configure_request_value_t, opaque), sizeof(wl_codec_bytes_t), 0U, 4U, { 18U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 0, 2U, 1U, offsetof(configure_request_value_t, backup), offsetof(configure_request_value_t, has_backup), 0, 0, sizeof(settings_value_t), 0U, 0U, { 26U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_value_desc.wire },
};
static const device_rpc_wlc_value_desc_t device_rpc_configure_request_value_desc = { { device_rpc_configure_request_value_fields, sizeof(device_rpc_configure_request_value_fields) / sizeof(device_rpc_configure_request_value_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR }, sizeof(configure_request_value_t), device_rpc_configure_request_value_decode_defaults };

static wl_codec_status_t device_rpc_configure_request_value_borrow(const configure_request_value_t *, configure_request_t *);
static inline wl_codec_status_t device_rpc_configure_request_wlc_detail_value_decode(const uint8_t *input, size_t length, configure_request_value_t *out) { return device_rpc_wlc_value_decode(&device_rpc_configure_request_value_desc.wire, input, length, out); }

static void device_rpc_configure_request_value_copy_fields(const configure_request_t *view, configure_request_value_t *out) {
  out->has_settings = view->has_settings;
  if (view->has_settings) device_rpc_settings_value_copy_fields(&view->settings, &out->settings);
  else device_rpc_settings_value_defaults(&out->settings);
  out->has_opaque = view->has_opaque;
  if (view->has_opaque) {
    out->opaque.length = view->opaque.length;
    if (view->opaque.length != 0U) memcpy(out->opaque.data, view->opaque.data, view->opaque.length);
  } else {
  }
  out->has_backup = view->has_backup;
  if (view->has_backup) device_rpc_settings_value_copy_fields(&view->backup, &out->backup);
  else device_rpc_settings_value_defaults(&out->backup);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
static inline void device_rpc_configure_request_wlc_detail_value_copy(const configure_request_t *view, configure_request_value_t *out) {
  memset(out, 0, sizeof(*out));
  device_rpc_configure_request_value_copy_fields(view, out);
}

static inline void device_rpc_configure_request_value_clear(configure_request_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  device_rpc_configure_request_value_defaults(value);
}

static inline wl_codec_status_t device_rpc_configure_request_value_from_view(const configure_request_t *view, configure_request_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_request_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  device_rpc_configure_request_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t device_rpc_configure_request_value_borrow(const configure_request_value_t *value, configure_request_t *out) {
  device_rpc_configure_request_clear(out);
  out->has_settings = value->has_settings;
  if (value->has_settings) {
    wl_codec_status_t status = device_rpc_settings_value_borrow(&value->settings, &out->settings);
    if (status != WL_CODEC_OK) return status;
  }
  out->has_opaque = value->has_opaque;
  if (value->has_opaque) {
    if (value->opaque.length > 4U) return WL_CODEC_ERR_INVALID_VALUE;
    out->opaque.length = value->opaque.length;
    out->opaque.data = value->opaque.data;
  }
  out->has_backup = value->has_backup;
  if (value->has_backup) {
    wl_codec_status_t status = device_rpc_settings_value_borrow(&value->backup, &out->backup);
    if (status != WL_CODEC_OK) return status;
  }
  return WL_CODEC_OK;
}

static inline wl_codec_status_t device_rpc_configure_request_value_to_view(const configure_request_value_t *value, configure_request_t *out) {
  configure_request_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_request_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = device_rpc_configure_request_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

static inline size_t device_rpc_configure_request_value_encoded_size(const configure_request_value_t *value) {
  configure_request_t view;
  if (value == NULL || device_rpc_configure_request_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return device_rpc_configure_request_encoded_size(&view);
}

static inline wl_codec_status_t device_rpc_configure_request_value_encode(const configure_request_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  configure_request_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_request_value_borrow(value, &view);
  return status == WL_CODEC_OK ? device_rpc_configure_request_encode(&view, out, capacity, length) : status;
}

static inline wl_codec_status_t device_rpc_configure_request_value_decode(const uint8_t *input, size_t length, configure_request_value_t *out) {
  configure_request_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_request_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

static void device_rpc_configure_response_value_defaults(configure_response_value_t *out) {
  (void)out;
  device_rpc_settings_value_defaults(&out->settings);
  device_rpc_settings_value_defaults(&out->backup);
  out->count = UINT32_C(0);
}

static void device_rpc_configure_response_value_decode_defaults(void *out) { device_rpc_configure_response_value_defaults(out); }

static const device_rpc_wlc_field_t device_rpc_configure_response_value_fields[] = {
  { 1U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 1, 2U, 1U, offsetof(configure_response_value_t, settings), offsetof(configure_response_value_t, has_settings), 0, 0, sizeof(settings_value_t), 0U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_value_desc.wire },
  { 2U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_BYTES, 0, 2U, 1U, offsetof(configure_response_value_t, opaque), offsetof(configure_response_value_t, has_opaque), 0, offsetof(configure_response_value_t, opaque.data) - offsetof(configure_response_value_t, opaque), sizeof(wl_codec_bytes_t), 0U, 4U, { 18U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_MESSAGE, 0, 2U, 1U, offsetof(configure_response_value_t, backup), offsetof(configure_response_value_t, has_backup), 0, 0, sizeof(settings_value_t), 0U, 0U, { 26U, 0U, 0U }, 0, 0ULL, NULL, &device_rpc_settings_value_desc.wire },
  { 4U, DEVICE_RPC_WLC_OPTIONAL, DEVICE_RPC_WLC_U32, 1, 0U, 1U, offsetof(configure_response_value_t, count), offsetof(configure_response_value_t, has_count), 0, 0, sizeof(uint32_t), 0U, 0U, { 32U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const device_rpc_wlc_value_desc_t device_rpc_configure_response_value_desc = { { device_rpc_configure_response_value_fields, sizeof(device_rpc_configure_response_value_fields) / sizeof(device_rpc_configure_response_value_fields[0]), DEVICE_RPC_WLC_LOOKUP_LINEAR }, sizeof(configure_response_value_t), device_rpc_configure_response_value_decode_defaults };

static wl_codec_status_t device_rpc_configure_response_value_borrow(const configure_response_value_t *, configure_response_t *);
static inline wl_codec_status_t device_rpc_configure_response_wlc_detail_value_decode(const uint8_t *input, size_t length, configure_response_value_t *out) { return device_rpc_wlc_value_decode(&device_rpc_configure_response_value_desc.wire, input, length, out); }

static void device_rpc_configure_response_value_copy_fields(const configure_response_t *view, configure_response_value_t *out) {
  out->has_settings = view->has_settings;
  if (view->has_settings) device_rpc_settings_value_copy_fields(&view->settings, &out->settings);
  else device_rpc_settings_value_defaults(&out->settings);
  out->has_opaque = view->has_opaque;
  if (view->has_opaque) {
    out->opaque.length = view->opaque.length;
    if (view->opaque.length != 0U) memcpy(out->opaque.data, view->opaque.data, view->opaque.length);
  } else {
  }
  out->has_backup = view->has_backup;
  if (view->has_backup) device_rpc_settings_value_copy_fields(&view->backup, &out->backup);
  else device_rpc_settings_value_defaults(&out->backup);
  out->has_count = view->has_count;
  out->count = view->has_count ? view->count : UINT32_C(0);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
static inline void device_rpc_configure_response_wlc_detail_value_copy(const configure_response_t *view, configure_response_value_t *out) {
  memset(out, 0, sizeof(*out));
  device_rpc_configure_response_value_copy_fields(view, out);
}

static inline void device_rpc_configure_response_value_clear(configure_response_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  device_rpc_configure_response_value_defaults(value);
}

static inline wl_codec_status_t device_rpc_configure_response_value_from_view(const configure_response_t *view, configure_response_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_response_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  device_rpc_configure_response_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t device_rpc_configure_response_value_borrow(const configure_response_value_t *value, configure_response_t *out) {
  device_rpc_configure_response_clear(out);
  out->has_settings = value->has_settings;
  if (value->has_settings) {
    wl_codec_status_t status = device_rpc_settings_value_borrow(&value->settings, &out->settings);
    if (status != WL_CODEC_OK) return status;
  }
  out->has_opaque = value->has_opaque;
  if (value->has_opaque) {
    if (value->opaque.length > 4U) return WL_CODEC_ERR_INVALID_VALUE;
    out->opaque.length = value->opaque.length;
    out->opaque.data = value->opaque.data;
  }
  out->has_backup = value->has_backup;
  if (value->has_backup) {
    wl_codec_status_t status = device_rpc_settings_value_borrow(&value->backup, &out->backup);
    if (status != WL_CODEC_OK) return status;
  }
  out->has_count = value->has_count;
  if (value->has_count) {
    out->count = value->count;
  }
  return WL_CODEC_OK;
}

static inline wl_codec_status_t device_rpc_configure_response_value_to_view(const configure_response_value_t *value, configure_response_t *out) {
  configure_response_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_response_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = device_rpc_configure_response_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

static inline size_t device_rpc_configure_response_value_encoded_size(const configure_response_value_t *value) {
  configure_response_t view;
  if (value == NULL || device_rpc_configure_response_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return device_rpc_configure_response_encoded_size(&view);
}

static inline wl_codec_status_t device_rpc_configure_response_value_encode(const configure_response_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  configure_response_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_response_value_borrow(value, &view);
  return status == WL_CODEC_OK ? device_rpc_configure_response_encode(&view, out, capacity, length) : status;
}

static inline wl_codec_status_t device_rpc_configure_response_value_decode(const uint8_t *input, size_t length, configure_response_value_t *out) {
  configure_response_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = device_rpc_configure_response_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

#endif
