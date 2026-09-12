#include "device.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

enum {
  WLC_OPTIONAL,
  WLC_REPEATED,
  WLC_PACKED,
  WLC_BOOL,
  WLC_U8,
  WLC_U16,
  WLC_U32,
  WLC_U64,
  WLC_I8,
  WLC_I16,
  WLC_I32,
  WLC_I64,
  WLC_F32,
  WLC_F64,
  WLC_FLOAT32,
  WLC_FLOAT64,
  WLC_BYTES,
  WLC_STRING,
  WLC_ENUM,
  WLC_MESSAGE
};
typedef struct wlc_desc wlc_desc_t;
typedef struct {
  uint16_t number;
  uint8_t card, kind, required, wire, key_size;
  size_t value, has, count, capacity, element, packed_count;
  uint16_t max_length;
  uint8_t key[3];
  int64_t signed_default;
  uint64_t unsigned_default;
  const char *string_default;
  const wlc_desc_t *nested;
} wlc_field_t;
enum { WLC_LOOKUP_LINEAR, WLC_LOOKUP_DENSE, WLC_LOOKUP_BINARY };
struct wlc_desc { const wlc_field_t *fields; size_t count; uint8_t lookup; };

static inline wl_codec_status_t wlc_add(size_t *a, size_t b) {
  if (b > SIZE_MAX - *a) return WL_CODEC_ERR_OVERFLOW;
  *a += b;
  return WL_CODEC_OK;
}
static inline size_t wlc_vsize(uint64_t v) {
  size_t n = 1U;
  while (v >= 128U) { v >>= 7U; ++n; }
  return n;
}
static inline void wlc_putv(uint8_t **p, uint64_t v) {
  while (v >= 128U) { *(*p)++ = (uint8_t)(v | 128U); v >>= 7U; }
  *(*p)++ = (uint8_t)v;
}
static wl_codec_status_t wlc_getv(const uint8_t *in, size_t length, size_t *at,
                                  uint64_t *out) {
  size_t start = *at, n = 0U;
  uint64_t v = 0U;
  while (*at < length && n < 10U) {
    uint8_t b = in[(*at)++];
    if (n == 9U && b > 1U) return WL_CODEC_ERR_OVERFLOW;
    v |= (uint64_t)(b & 127U) << (7U * n++);
    if ((b & 128U) == 0U) {
      if (wlc_vsize(v) != *at - start) return WL_CODEC_ERR_MALFORMED;
      *out = v;
      return WL_CODEC_OK;
    }
  }
  return *at == length ? WL_CODEC_ERR_MALFORMED : WL_CODEC_ERR_OVERFLOW;
}
static bool wlc_utf8(const uint8_t *s, size_t n) {
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
static inline uint8_t wlc_wire(const wlc_field_t *f) {
  return f->wire;
}
static inline uint64_t wlc_z32(int32_t v) {
  return ((uint32_t)v << 1U) ^ (uint32_t)-(uint32_t)(v < 0);
}
static inline uint64_t wlc_z64(int64_t v) {
  return ((uint64_t)v << 1U) ^ (uint64_t)-(uint64_t)(v < 0);
}
static inline int32_t wlc_uz32(uint32_t v) {
  return (int32_t)((v >> 1U) ^ (uint32_t)-(v & 1U));
}
static inline int64_t wlc_uz64(uint64_t v) {
  return (int64_t)((v >> 1U) ^ (uint64_t)-(v & 1U));
}
static wl_codec_status_t wlc_measure(const wlc_desc_t *, const void *, size_t *);
static wl_codec_status_t wlc_measure_impl(const wlc_desc_t *, const void *, size_t *, bool);
static void wlc_clear(const wlc_desc_t *, void *);
static wl_codec_status_t wlc_decode(const wlc_desc_t *, const uint8_t *, size_t,
                                    void *);
static wl_codec_status_t wlc_emit_fields(const wlc_desc_t *, const void *,
                                         uint8_t **);

static wl_codec_status_t wlc_packed_bytes(const wlc_field_t *f, size_t *bytes) {
  if (f->packed_count != 0U && f->element > SIZE_MAX / f->packed_count)
    return WL_CODEC_ERR_OVERFLOW;
  *bytes = f->element * f->packed_count;
  return WL_CODEC_OK;
}
static wl_codec_status_t wlc_body(const wlc_field_t *f, const void *p,
                                  size_t *n, bool validated) {
  *n = 0U;
  switch (f->kind) {
    case WLC_BOOL: {
      bool v = *(const bool *)p;
      if (v != false && v != true) return WL_CODEC_ERR_INVALID_VALUE;
      *n = 1U;
      return WL_CODEC_OK;
    }
    case WLC_U8: *n = wlc_vsize(*(const uint8_t *)p); return WL_CODEC_OK;
    case WLC_U16: *n = wlc_vsize(*(const uint16_t *)p); return WL_CODEC_OK;
    case WLC_U32: *n = wlc_vsize(*(const uint32_t *)p); return WL_CODEC_OK;
    case WLC_U64: *n = wlc_vsize(*(const uint64_t *)p); return WL_CODEC_OK;
    case WLC_I8: *n = wlc_vsize(wlc_z32(*(const int8_t *)p)); return WL_CODEC_OK;
    case WLC_I16: *n = wlc_vsize(wlc_z32(*(const int16_t *)p)); return WL_CODEC_OK;
    case WLC_I32:
    case WLC_ENUM: *n = wlc_vsize(wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    case WLC_I64: *n = wlc_vsize(wlc_z64(*(const int64_t *)p)); return WL_CODEC_OK;
    case WLC_F32:
    case WLC_FLOAT32: *n = 4U; return WL_CODEC_OK;
    case WLC_F64:
    case WLC_FLOAT64: *n = 8U; return WL_CODEC_OK;
    case WLC_BYTES: {
      const wl_codec_bytes_t *v = p;
      if (f->max_length != 0U && v->length > (size_t)f->max_length)
        return WL_CODEC_ERR_INVALID_VALUE;
      if (v->length != 0U && v->data == NULL) return WL_CODEC_ERR_INVALID_VALUE;
      if (wlc_add(n, wlc_vsize(v->length)) != WL_CODEC_OK)
        return WL_CODEC_ERR_OVERFLOW;
      return wlc_add(n, v->length);
    }
    case WLC_STRING: {
      const wl_codec_string_t *v = p;
      if (f->max_length != 0U && v->length > (size_t)f->max_length)
        return WL_CODEC_ERR_INVALID_VALUE;
      if (v->length != 0U && v->data == NULL) return WL_CODEC_ERR_INVALID_VALUE;
      if (!validated && !wlc_utf8((const uint8_t *)v->data, v->length)) return WL_CODEC_ERR_UTF8;
      if (wlc_add(n, wlc_vsize(v->length)) != WL_CODEC_OK)
        return WL_CODEC_ERR_OVERFLOW;
      return wlc_add(n, v->length);
    }
    case WLC_MESSAGE: {
      size_t child;
      wl_codec_status_t s = wlc_measure_impl(f->nested, p, &child, validated);
      if (s != WL_CODEC_OK) return s;
      *n = wlc_vsize(child);
      return wlc_add(n, child);
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
static wl_codec_status_t wlc_measure_impl(const wlc_desc_t *d, const void *value,
                                     size_t *out, bool validated) {
  size_t n = 0U;
  if (d == NULL || value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
    if (f->card == WLC_PACKED) {
      size_t bytes;
      wl_codec_status_t s;
      if (!*(const bool *)(base + f->has)) {
        if (f->required != 0U) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
        continue;
      }
      if ((s = wlc_packed_bytes(f, &bytes)) != WL_CODEC_OK) return s;
      if ((s = wlc_add(&n, f->key_size)) != WL_CODEC_OK ||
          (s = wlc_add(&n, wlc_vsize(bytes))) != WL_CODEC_OK ||
          (s = wlc_add(&n, bytes)) != WL_CODEC_OK) return s;
      continue;
    }
    size_t count = 1U;
    if (f->card == WLC_OPTIONAL) {
      if (!*(const bool *)(base + f->has)) {
        if (f->required != 0U) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
        continue;
      }
    } else {
      count = *(const size_t *)(base + f->count);
      if ((count != 0U && *(void *const *)(base + f->value) == NULL) ||
          count > *(const size_t *)(base + f->capacity))
        return WL_CODEC_ERR_INVALID_VALUE;
    }
    for (size_t j = 0U; j < count; ++j) {
      size_t body;
      const void *p = f->card == WLC_REPEATED
                          ? *(const uint8_t *const *)(base + f->value) + j * f->element
                          : base + f->value;
      wl_codec_status_t s = wlc_body(f, p, &body, validated);
      if (s != WL_CODEC_OK) return s;
      if ((s = wlc_add(&n, f->key_size)) != WL_CODEC_OK ||
          (s = wlc_add(&n, body)) != WL_CODEC_OK) return s;
    }
  }
  *out = n;
  return WL_CODEC_OK;
}
static wl_codec_status_t wlc_measure(const wlc_desc_t *d, const void *value, size_t *out) {
  return wlc_measure_impl(d, value, out, false);
}
static wl_codec_status_t wlc_measure_validated(const wlc_desc_t *d, const void *value, size_t *out) {
  return wlc_measure_impl(d, value, out, true);
}
static void wlc_clear(const wlc_desc_t *d, void *value) {
  uint8_t *base = value;
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    void *p = base + f->value;
    if (f->card == WLC_REPEATED) {
      *(size_t *)(base + f->count) = 0U;
      continue;
    }
    *(bool *)(base + f->has) = false;
    if (f->card == WLC_PACKED) {
      memset(p, 0, f->element * f->packed_count);
      continue;
    }
    if (f->kind == WLC_MESSAGE) { wlc_clear(f->nested, p); continue; }
    if (f->kind == WLC_STRING) {
      *(wl_codec_string_t *)p =
          (wl_codec_string_t){f->string_default, (size_t)f->unsigned_default};
      continue;
    }
    if (f->kind == WLC_BYTES) {
      *(wl_codec_bytes_t *)p = (wl_codec_bytes_t){NULL, 0U};
      continue;
    }
    if (f->kind == WLC_FLOAT32 || f->kind == WLC_FLOAT64) {
      memset(p, 0, f->element);
      continue;
    }
    if (f->kind == WLC_BOOL) *(bool *)p = f->unsigned_default != 0U;
    else if (f->kind == WLC_I8) *(int8_t *)p = (int8_t)f->signed_default;
    else if (f->kind == WLC_I16) *(int16_t *)p = (int16_t)f->signed_default;
    else if (f->kind == WLC_I32 || f->kind == WLC_ENUM)
      *(int32_t *)p = (int32_t)f->signed_default;
    else if (f->kind == WLC_I64) *(int64_t *)p = f->signed_default;
    else if (f->kind == WLC_U8) *(uint8_t *)p = (uint8_t)f->unsigned_default;
    else if (f->kind == WLC_U16) *(uint16_t *)p = (uint16_t)f->unsigned_default;
    else if (f->kind == WLC_U32 || f->kind == WLC_F32)
      *(uint32_t *)p = (uint32_t)f->unsigned_default;
    else if (f->kind == WLC_U64 || f->kind == WLC_F64)
      *(uint64_t *)p = f->unsigned_default;
  }
}
static inline void wlc_put32(uint8_t **p, uint32_t v) {
  *(*p)++ = (uint8_t)(v >> 24U);
  *(*p)++ = (uint8_t)(v >> 16U);
  *(*p)++ = (uint8_t)(v >> 8U);
  *(*p)++ = (uint8_t)v;
}
static inline void wlc_put64(uint8_t **p, uint64_t v) {
  wlc_put32(p, (uint32_t)(v >> 32U));
  wlc_put32(p, (uint32_t)v);
}
static inline void wlc_copy_span(uint8_t **out, const void *data, size_t length) {
  if (length != 0U) { memcpy(*out, data, length); *out += length; }
}
static inline void wlc_put_key(uint8_t **out, const wlc_field_t *f) {
  *(*out)++ = f->key[0];
  if (f->key_size > 1U) {
    *(*out)++ = f->key[1];
    if (f->key_size > 2U) *(*out)++ = f->key[2];
  }
}

static wl_codec_status_t wlc_emit_fixed(uint8_t kind, const void *value,
                                        uint8_t **out) {
  if (kind == WLC_F32) wlc_put32(out, *(const uint32_t *)value);
  else if (kind == WLC_F64) wlc_put64(out, *(const uint64_t *)value);
  else if (kind == WLC_FLOAT32) {
    uint32_t bits32;
    memcpy(&bits32, value, sizeof(bits32));
    wlc_put32(out, bits32);
  } else if (kind == WLC_FLOAT64) {
    uint64_t bits;
    memcpy(&bits, value, sizeof(bits));
    wlc_put64(out, bits);
  } else return WL_CODEC_ERR_INVALID_VALUE;
  return WL_CODEC_OK;
}
static wl_codec_status_t wlc_emit_value(const wlc_field_t *f, const void *p,
                                        uint8_t **out) {
  switch (f->kind) {
    case WLC_BOOL: wlc_putv(out, *(const bool *)p); return WL_CODEC_OK;
    case WLC_U8: wlc_putv(out, *(const uint8_t *)p); return WL_CODEC_OK;
    case WLC_U16: wlc_putv(out, *(const uint16_t *)p); return WL_CODEC_OK;
    case WLC_U32: wlc_putv(out, *(const uint32_t *)p); return WL_CODEC_OK;
    case WLC_U64: wlc_putv(out, *(const uint64_t *)p); return WL_CODEC_OK;
    case WLC_I8: wlc_putv(out, wlc_z32(*(const int8_t *)p)); return WL_CODEC_OK;
    case WLC_I16: wlc_putv(out, wlc_z32(*(const int16_t *)p)); return WL_CODEC_OK;
    case WLC_I32:
    case WLC_ENUM: wlc_putv(out, wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    case WLC_I64: wlc_putv(out, wlc_z64(*(const int64_t *)p)); return WL_CODEC_OK;
    case WLC_F32:
    case WLC_F64:
    case WLC_FLOAT32:
    case WLC_FLOAT64: return wlc_emit_fixed(f->kind, p, out);
    case WLC_BYTES: {
      const wl_codec_bytes_t *v = p;
      wlc_putv(out, v->length);
      wlc_copy_span(out, v->data, v->length);
      return WL_CODEC_OK;
    }
    case WLC_STRING: {
      const wl_codec_string_t *v = p;
      wlc_putv(out, v->length);
      wlc_copy_span(out, v->data, v->length);
      return WL_CODEC_OK;
    }
    case WLC_MESSAGE: {
      size_t child;
      wl_codec_status_t s = wlc_measure(f->nested, p, &child);
      if (s != WL_CODEC_OK) return s;
      wlc_putv(out, child);
      return wlc_emit_fields(f->nested, p, out);
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
static wl_codec_status_t wlc_emit_packed(const wlc_field_t *f, const void *p,
                                         uint8_t **out) {
  size_t bytes;
  wl_codec_status_t s = wlc_packed_bytes(f, &bytes);
  if (s != WL_CODEC_OK) return s;
  wlc_putv(out, bytes);
  if (f->kind == WLC_F32 || f->kind == WLC_FLOAT32) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint32_t bits;
      memcpy(&bits, (const uint8_t *)p + j * f->element, sizeof(bits));
      wlc_put32(out, bits);
    }
  } else if (f->kind == WLC_F64 || f->kind == WLC_FLOAT64) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint64_t bits;
      memcpy(&bits, (const uint8_t *)p + j * f->element, sizeof(bits));
      wlc_put64(out, bits);
    }
  } else {
    return WL_CODEC_ERR_INVALID_VALUE;
  }
  return WL_CODEC_OK;
}
static wl_codec_status_t wlc_emit_fields(const wlc_desc_t *d, const void *value,
                                         uint8_t **out) {
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
    if (f->card == WLC_PACKED) {
      wl_codec_status_t s;
      if (!*(const bool *)(base + f->has)) continue;
      wlc_put_key(out, f);
      if ((s = wlc_emit_packed(f, base + f->value, out)) != WL_CODEC_OK) return s;
      continue;
    }
    size_t count = f->card == WLC_OPTIONAL
                       ? (*(const bool *)(base + f->has) ? 1U : 0U)
                       : *(const size_t *)(base + f->count);
    for (size_t j = 0U; j < count; ++j) {
      const void *p = f->card == WLC_REPEATED
                          ? *(const uint8_t *const *)(base + f->value) + j * f->element
                          : base + f->value;
      wl_codec_status_t s;
      wlc_put_key(out, f);
      if ((s = wlc_emit_value(f, p, out)) != WL_CODEC_OK) return s;
    }
  }
  return WL_CODEC_OK;
}

static inline wl_codec_status_t wlc_encode(const wlc_desc_t *d, const void *value,
                                    uint8_t *out, size_t cap, size_t *length) {
  size_t n;
  wl_codec_status_t s = wlc_measure(d, value, &n);
  if (s != WL_CODEC_OK || length == NULL || (n != 0U && out == NULL))
    return s == WL_CODEC_OK ? WL_CODEC_ERR_INVALID_VALUE : s;
  if (cap < n) return WL_CODEC_ERR_CAPACITY;
  uint8_t *p = out;
  if ((s = wlc_emit_fields(d, value, &p)) != WL_CODEC_OK) return s;
  *length = n;
  return WL_CODEC_OK;
}
static wl_codec_status_t wlc_skip(uint8_t wire, const uint8_t *in, size_t n,
                                  size_t *at) {
  uint64_t length;
  wl_codec_status_t s;
  if (wire == 0U) return wlc_getv(in, n, at, &length);
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
  if ((s = wlc_getv(in, n, at, &length)) != WL_CODEC_OK) return s;
  if (length > n - *at) return WL_CODEC_ERR_MALFORMED;
  *at += (size_t)length;
  return WL_CODEC_OK;
}
static wl_codec_status_t wlc_read_fixed(uint8_t kind, const uint8_t *in,
                                        size_t n, size_t *at, void *out) {
  size_t bytes = (kind == WLC_F32 || kind == WLC_FLOAT32) ? 4U : 8U;
  if (n - *at < bytes) return WL_CODEC_ERR_MALFORMED;
  uint64_t bits = 0U;
  for (size_t i = 0U; i < bytes; ++i) bits = (bits << 8U) | in[(*at)++];
  if (kind == WLC_F32) *(uint32_t *)out = (uint32_t)bits;
  else if (kind == WLC_F64) *(uint64_t *)out = bits;
  else if (kind == WLC_FLOAT32) {
    uint32_t bits32 = (uint32_t)bits;
    memcpy(out, &bits32, sizeof(bits32));
  } else if (kind == WLC_FLOAT64) memcpy(out, &bits, sizeof(bits));
  else return WL_CODEC_ERR_INVALID_VALUE;
  return WL_CODEC_OK;
}
static wl_codec_status_t wlc_read_value(const wlc_field_t *f, const uint8_t *in,
                                        size_t n, size_t *at, void *out) {
  uint64_t v;
  wl_codec_status_t s;
  if (f->kind == WLC_F32 || f->kind == WLC_F64 ||
      f->kind == WLC_FLOAT32 || f->kind == WLC_FLOAT64)
    return wlc_read_fixed(f->kind, in, n, at, out);
  if (f->kind == WLC_BYTES || f->kind == WLC_STRING || f->kind == WLC_MESSAGE) {
    if ((s = wlc_getv(in, n, at, &v)) != WL_CODEC_OK) return s;
    if ((f->kind == WLC_BYTES || f->kind == WLC_STRING) &&
        f->max_length != 0U && v > (uint64_t)f->max_length)
      return WL_CODEC_ERR_INVALID_VALUE;
    if (v > n - *at) return WL_CODEC_ERR_MALFORMED;
    size_t bytes = (size_t)v;
    if (f->kind == WLC_BYTES)
      *(wl_codec_bytes_t *)out = (wl_codec_bytes_t){in + *at, bytes};
    else if (f->kind == WLC_STRING) {
      if (!wlc_utf8(in + *at, bytes)) return WL_CODEC_ERR_UTF8;
      *(wl_codec_string_t *)out =
          (wl_codec_string_t){(const char *)(in + *at), bytes};
    } else {
      s = wlc_decode(f->nested, in + *at, bytes, out);
      if (s != WL_CODEC_OK) return s;
    }
    *at += bytes;
    return WL_CODEC_OK;
  }
  if ((s = wlc_getv(in, n, at, &v)) != WL_CODEC_OK) return s;
  if (f->kind == WLC_BOOL) {
    if (v > 1U) return WL_CODEC_ERR_INVALID_VALUE;
    *(bool *)out = v != 0U;
  } else if (f->kind == WLC_U8) {
    if (v > UINT8_MAX) return WL_CODEC_ERR_OVERFLOW;
    *(uint8_t *)out = (uint8_t)v;
  } else if (f->kind == WLC_U16) {
    if (v > UINT16_MAX) return WL_CODEC_ERR_OVERFLOW;
    *(uint16_t *)out = (uint16_t)v;
  } else if (f->kind == WLC_U32) {
    if (v > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
    *(uint32_t *)out = (uint32_t)v;
  } else if (f->kind == WLC_U64) *(uint64_t *)out = v;
  else if (f->kind == WLC_I8) {
    if (v > UINT8_MAX) return WL_CODEC_ERR_OVERFLOW;
    *(int8_t *)out = (int8_t)wlc_uz32((uint32_t)v);
  } else if (f->kind == WLC_I16) {
    if (v > UINT16_MAX) return WL_CODEC_ERR_OVERFLOW;
    *(int16_t *)out = (int16_t)wlc_uz32((uint32_t)v);
  } else if (f->kind == WLC_I32 || f->kind == WLC_ENUM) {
    if (v > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
    *(int32_t *)out = wlc_uz32((uint32_t)v);
  } else if (f->kind == WLC_I64) *(int64_t *)out = wlc_uz64(v);
  else return WL_CODEC_ERR_INVALID_VALUE;
  return WL_CODEC_OK;
}
static wl_codec_status_t wlc_read_packed(const wlc_field_t *f,
                                         const uint8_t *in, size_t n,
                                         size_t *at, void *out) {
  uint64_t encoded_bytes;
  size_t expected_bytes;
  wl_codec_status_t s;
  if ((s = wlc_getv(in, n, at, &encoded_bytes)) != WL_CODEC_OK) return s;
  if ((s = wlc_packed_bytes(f, &expected_bytes)) != WL_CODEC_OK) return s;
  if (encoded_bytes != expected_bytes || expected_bytes > n - *at)
    return WL_CODEC_ERR_MALFORMED;
  if (f->kind == WLC_F32 || f->kind == WLC_FLOAT32) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint32_t bits = ((uint32_t)in[*at] << 24U) |
                      ((uint32_t)in[*at + 1U] << 16U) |
                      ((uint32_t)in[*at + 2U] << 8U) |
                      (uint32_t)in[*at + 3U];
      memcpy((uint8_t *)out + j * f->element, &bits, sizeof(bits));
      *at += 4U;
    }
  } else if (f->kind == WLC_F64 || f->kind == WLC_FLOAT64) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint64_t bits = 0U;
      for (size_t i = 0U; i < 8U; ++i) bits = (bits << 8U) | in[(*at)++];
      memcpy((uint8_t *)out + j * f->element, &bits, sizeof(bits));
    }
  } else {
    return WL_CODEC_ERR_INVALID_VALUE;
  }
  return WL_CODEC_OK;
}
static const wlc_field_t *wlc_find_field(const wlc_desc_t *d, uint16_t number) {
  if (d->lookup == WLC_LOOKUP_DENSE) {
    size_t index = (size_t)number - (size_t)d->fields[0].number;
    return index < d->count ? &d->fields[index] : NULL;
  }
  if (d->lookup == WLC_LOOKUP_BINARY) {
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
static wl_codec_status_t wlc_decode(const wlc_desc_t *d, const uint8_t *in,
                                    size_t n, void *out) {
  if (d == NULL || out == NULL || (n != 0U && in == NULL))
    return WL_CODEC_ERR_INVALID_VALUE;
  wlc_clear(d, out);
  // A hint only: missing, unknown or reordered fields still use exact lookup.
  // Each recursive decode owns its cursor; empty schemas avoid NULL arithmetic.
  const wlc_field_t *next = d->fields;
  const wlc_field_t *end = d->count == 0U ? next : next + d->count;
  for (size_t at = 0U; at < n;) {
    uint64_t key;
    wl_codec_status_t s = wlc_getv(in, n, &at, &key);
    if (s != WL_CODEC_OK) return s;
    uint64_t raw_number = key >> 3U;
    uint8_t wire = (uint8_t)(key & 7U);
    if (raw_number == 0U || raw_number > 65535U ||
        (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    uint16_t number = (uint16_t)raw_number;
    const wlc_field_t *f = next != end && next->number == number
                               ? next : wlc_find_field(d, number);
    if (f == NULL) {
      if ((s = wlc_skip(wire, in, n, &at)) != WL_CODEC_OK) return s;
      continue;
    }
    if (wire != wlc_wire(f)) return WL_CODEC_ERR_WIRE_TYPE;
    next = f->card == WLC_REPEATED ? f : f + 1;
    uint8_t *base = out;
    if (f->card == WLC_PACKED) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
      if ((s = wlc_read_packed(f, in, n, &at, base + f->value)) != WL_CODEC_OK)
        return s;
      *(bool *)(base + f->has) = true;
      continue;
    }
    void *p;
    if (f->card == WLC_OPTIONAL) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
      p = base + f->value;
    } else {
      size_t count = *(size_t *)(base + f->count);
      if (count >= *(size_t *)(base + f->capacity)) return WL_CODEC_ERR_CAPACITY;
      void *storage = *(void **)(base + f->value);
      if (storage == NULL) return WL_CODEC_ERR_INVALID_VALUE;
      p = (uint8_t *)storage + count * f->element;
    }
    if ((s = wlc_read_value(f, in, n, &at, p)) != WL_CODEC_OK) return s;
    if (f->card == WLC_OPTIONAL) *(bool *)(base + f->has) = true;
    else ++*(size_t *)(base + f->count);
  }
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    if (f->required != 0U && !*(const bool *)((uint8_t *)out + f->has))
      return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  }
  return WL_CODEC_OK;
}
/* Generator-private canonical sink. Only successful decode results enter here.
 * The shared emitter orders known fields, drops unknowns, and preserves presence.
 * Scalar bytes use the ordinary endian/varint writers; no raw-frame hashing. */
typedef struct {
  uint64_t hash;
  size_t length;
  wl_codec_status_t status;
} wlc_hash_state_t;
static wl_codec_status_t wlc_hash_emit_fields(const wlc_desc_t *, const void *, wlc_hash_state_t *);
static inline void wlc_hash_copy_span(wlc_hash_state_t *out, const void *data, size_t length) {
  const uint8_t *bytes = data;
  if (out->status != WL_CODEC_OK) return;
  out->status = wlc_add(&out->length, length);
  if (out->status != WL_CODEC_OK) return;
  uint64_t hash = out->hash;
  for (size_t i = 0U; i < length; ++i)
    hash = (hash ^ bytes[i]) * UINT64_C(0x100000001b3);
  out->hash = hash;
}
static inline void wlc_hash_putv(wlc_hash_state_t *out, uint64_t value) {
  uint8_t bytes[10], *cursor = bytes;
  wlc_putv(&cursor, value);
  wlc_hash_copy_span(out, bytes, (size_t)(cursor - bytes));
}
static inline void wlc_hash_put_key(wlc_hash_state_t *out, const wlc_field_t *f) {
  wlc_hash_copy_span(out, f->key, f->key_size);
}
static inline void wlc_hash_put32(wlc_hash_state_t *out, uint32_t value) {
  uint8_t bytes[4], *cursor = bytes;
  wlc_put32(&cursor, value);
  wlc_hash_copy_span(out, bytes, sizeof(bytes));
}
static inline void wlc_hash_put64(wlc_hash_state_t *out, uint64_t value) {
  uint8_t bytes[8], *cursor = bytes;
  wlc_put64(&cursor, value);
  wlc_hash_copy_span(out, bytes, sizeof(bytes));
}

static wl_codec_status_t wlc_hash_emit_fixed(uint8_t kind, const void *value,
                                        wlc_hash_state_t *out) {
  if (kind == WLC_F32) wlc_hash_put32(out, *(const uint32_t *)value);
  else if (kind == WLC_F64) wlc_hash_put64(out, *(const uint64_t *)value);
  else if (kind == WLC_FLOAT32) {
    uint32_t bits32;
    memcpy(&bits32, value, sizeof(bits32));
    wlc_hash_put32(out, bits32);
  } else if (kind == WLC_FLOAT64) {
    uint64_t bits;
    memcpy(&bits, value, sizeof(bits));
    wlc_hash_put64(out, bits);
  } else return WL_CODEC_ERR_INVALID_VALUE;
  return WL_CODEC_OK;
}
static wl_codec_status_t wlc_hash_emit_value(const wlc_field_t *f, const void *p,
                                        wlc_hash_state_t *out) {
  switch (f->kind) {
    case WLC_BOOL: wlc_hash_putv(out, *(const bool *)p); return WL_CODEC_OK;
    case WLC_U8: wlc_hash_putv(out, *(const uint8_t *)p); return WL_CODEC_OK;
    case WLC_U16: wlc_hash_putv(out, *(const uint16_t *)p); return WL_CODEC_OK;
    case WLC_U32: wlc_hash_putv(out, *(const uint32_t *)p); return WL_CODEC_OK;
    case WLC_U64: wlc_hash_putv(out, *(const uint64_t *)p); return WL_CODEC_OK;
    case WLC_I8: wlc_hash_putv(out, wlc_z32(*(const int8_t *)p)); return WL_CODEC_OK;
    case WLC_I16: wlc_hash_putv(out, wlc_z32(*(const int16_t *)p)); return WL_CODEC_OK;
    case WLC_I32:
    case WLC_ENUM: wlc_hash_putv(out, wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    case WLC_I64: wlc_hash_putv(out, wlc_z64(*(const int64_t *)p)); return WL_CODEC_OK;
    case WLC_F32:
    case WLC_F64:
    case WLC_FLOAT32:
    case WLC_FLOAT64: return wlc_hash_emit_fixed(f->kind, p, out);
    case WLC_BYTES: {
      const wl_codec_bytes_t *v = p;
      wlc_hash_putv(out, v->length);
      wlc_hash_copy_span(out, v->data, v->length);
      return WL_CODEC_OK;
    }
    case WLC_STRING: {
      const wl_codec_string_t *v = p;
      wlc_hash_putv(out, v->length);
      wlc_hash_copy_span(out, v->data, v->length);
      return WL_CODEC_OK;
    }
    case WLC_MESSAGE: {
      size_t child;
      wl_codec_status_t s = wlc_measure_validated(f->nested, p, &child);
      if (s != WL_CODEC_OK) return s;
      wlc_hash_putv(out, child);
      return wlc_hash_emit_fields(f->nested, p, out);
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
static wl_codec_status_t wlc_hash_emit_packed(const wlc_field_t *f, const void *p,
                                         wlc_hash_state_t *out) {
  size_t bytes;
  wl_codec_status_t s = wlc_packed_bytes(f, &bytes);
  if (s != WL_CODEC_OK) return s;
  wlc_hash_putv(out, bytes);
  if (f->kind == WLC_F32 || f->kind == WLC_FLOAT32) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint32_t bits;
      memcpy(&bits, (const uint8_t *)p + j * f->element, sizeof(bits));
      wlc_hash_put32(out, bits);
    }
  } else if (f->kind == WLC_F64 || f->kind == WLC_FLOAT64) {
    for (size_t j = 0U; j < f->packed_count; ++j) {
      uint64_t bits;
      memcpy(&bits, (const uint8_t *)p + j * f->element, sizeof(bits));
      wlc_hash_put64(out, bits);
    }
  } else {
    return WL_CODEC_ERR_INVALID_VALUE;
  }
  return WL_CODEC_OK;
}
static wl_codec_status_t wlc_hash_emit_fields(const wlc_desc_t *d, const void *value,
                                         wlc_hash_state_t *out) {
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
    if (f->card == WLC_PACKED) {
      wl_codec_status_t s;
      if (!*(const bool *)(base + f->has)) continue;
      wlc_hash_put_key(out, f);
      if ((s = wlc_hash_emit_packed(f, base + f->value, out)) != WL_CODEC_OK) return s;
      continue;
    }
    size_t count = f->card == WLC_OPTIONAL
                       ? (*(const bool *)(base + f->has) ? 1U : 0U)
                       : *(const size_t *)(base + f->count);
    for (size_t j = 0U; j < count; ++j) {
      const void *p = f->card == WLC_REPEATED
                          ? *(const uint8_t *const *)(base + f->value) + j * f->element
                          : base + f->value;
      wl_codec_status_t s;
      wlc_hash_put_key(out, f);
      if ((s = wlc_hash_emit_value(f, p, out)) != WL_CODEC_OK) return s;
    }
  }
  return WL_CODEC_OK;
}
static const wlc_desc_t info_request_desc;
static const wlc_desc_t settings_desc;
static const wlc_desc_t info_response_desc;
static const wlc_desc_t configure_request_desc;
static const wlc_desc_t configure_response_desc;
static const wlc_desc_t numbers_desc;

static const wlc_desc_t info_request_desc = { NULL, 0U, WLC_LOOKUP_LINEAR };

static const wlc_field_t settings_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_STRING, 0, 2U, 1U, offsetof(settings_t, label), offsetof(settings_t, has_label), 0, 0, sizeof(wl_codec_string_t), 0U, 12U, { 10U, 0U, 0U }, 0, 6ULL, "\xE8""\xAE""\xBE""\xE5""\xA4""\x87", NULL },
  { 2U, WLC_OPTIONAL, WLC_BOOL, 1, 0U, 1U, offsetof(settings_t, enabled), offsetof(settings_t, has_enabled), 0, 0, sizeof(bool), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_ENUM, 0, 0U, 1U, offsetof(settings_t, mode), offsetof(settings_t, has_mode), 0, 0, sizeof(mode_t), 0U, 0U, { 24U, 0U, 0U }, INT32_C(1), 0ULL, NULL, NULL },
  { 4U, WLC_OPTIONAL, WLC_BYTES, 1, 2U, 1U, offsetof(settings_t, token), offsetof(settings_t, has_token), 0, 0, sizeof(wl_codec_bytes_t), 0U, 8U, { 34U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 5U, WLC_PACKED, WLC_FLOAT32, 1, 2U, 1U, offsetof(settings_t, gains), offsetof(settings_t, has_gains), 0, 0, sizeof(float), 3U, 0U, { 42U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 6U, WLC_PACKED, WLC_F64, 0, 2U, 1U, offsetof(settings_t, counters), offsetof(settings_t, has_counters), 0, 0, sizeof(uint64_t), 2U, 0U, { 50U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 7U, WLC_OPTIONAL, WLC_I64, 0, 0U, 1U, offsetof(settings_t, floor), offsetof(settings_t, has_floor), 0, 0, sizeof(int64_t), 0U, 0U, { 56U, 0U, 0U }, INT64_MIN, 0ULL, NULL, NULL },
  { 8U, WLC_OPTIONAL, WLC_U64, 0, 0U, 1U, offsetof(settings_t, ceiling), offsetof(settings_t, has_ceiling), 0, 0, sizeof(uint64_t), 0U, 0U, { 64U, 0U, 0U }, 0, 18446744073709551615ULL, NULL, NULL },
  { 9U, WLC_OPTIONAL, WLC_STRING, 0, 2U, 1U, offsetof(settings_t, class_), offsetof(settings_t, has_class_), 0, 0, sizeof(wl_codec_string_t), 0U, 8U, { 74U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 10U, WLC_OPTIONAL, WLC_U16, 0, 0U, 1U, offsetof(settings_t, timeout), offsetof(settings_t, has_timeout), 0, 0, sizeof(uint16_t), 0U, 0U, { 80U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t settings_desc = { settings_fields, sizeof(settings_fields) / sizeof(settings_fields[0]), WLC_LOOKUP_DENSE };

static const wlc_field_t info_response_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_STRING, 1, 2U, 1U, offsetof(info_response_t, name), offsetof(info_response_t, has_name), 0, 0, sizeof(wl_codec_string_t), 0U, 32U, { 10U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_MESSAGE, 1, 2U, 1U, offsetof(info_response_t, settings), offsetof(info_response_t, has_settings), 0, 0, sizeof(settings_t), 0U, 0U, { 18U, 0U, 0U }, 0, 0ULL, NULL, &settings_desc },
};
static const wlc_desc_t info_response_desc = { info_response_fields, sizeof(info_response_fields) / sizeof(info_response_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t configure_request_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_MESSAGE, 1, 2U, 1U, offsetof(configure_request_t, settings), offsetof(configure_request_t, has_settings), 0, 0, sizeof(settings_t), 0U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, &settings_desc },
  { 2U, WLC_OPTIONAL, WLC_BYTES, 0, 2U, 1U, offsetof(configure_request_t, opaque), offsetof(configure_request_t, has_opaque), 0, 0, sizeof(wl_codec_bytes_t), 0U, 4U, { 18U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_MESSAGE, 0, 2U, 1U, offsetof(configure_request_t, backup), offsetof(configure_request_t, has_backup), 0, 0, sizeof(settings_t), 0U, 0U, { 26U, 0U, 0U }, 0, 0ULL, NULL, &settings_desc },
};
static const wlc_desc_t configure_request_desc = { configure_request_fields, sizeof(configure_request_fields) / sizeof(configure_request_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t configure_response_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_MESSAGE, 1, 2U, 1U, offsetof(configure_response_t, settings), offsetof(configure_response_t, has_settings), 0, 0, sizeof(settings_t), 0U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, &settings_desc },
  { 2U, WLC_OPTIONAL, WLC_BYTES, 0, 2U, 1U, offsetof(configure_response_t, opaque), offsetof(configure_response_t, has_opaque), 0, 0, sizeof(wl_codec_bytes_t), 0U, 4U, { 18U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_MESSAGE, 0, 2U, 1U, offsetof(configure_response_t, backup), offsetof(configure_response_t, has_backup), 0, 0, sizeof(settings_t), 0U, 0U, { 26U, 0U, 0U }, 0, 0ULL, NULL, &settings_desc },
  { 4U, WLC_OPTIONAL, WLC_U32, 1, 0U, 1U, offsetof(configure_response_t, count), offsetof(configure_response_t, has_count), 0, 0, sizeof(uint32_t), 0U, 0U, { 32U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t configure_response_desc = { configure_response_fields, sizeof(configure_response_fields) / sizeof(configure_response_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t numbers_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_I8, 1, 0U, 1U, offsetof(numbers_t, i8), offsetof(numbers_t, has_i8), 0, 0, sizeof(int8_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_U8, 1, 0U, 1U, offsetof(numbers_t, u8), offsetof(numbers_t, has_u8), 0, 0, sizeof(uint8_t), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_I16, 1, 0U, 1U, offsetof(numbers_t, i16), offsetof(numbers_t, has_i16), 0, 0, sizeof(int16_t), 0U, 0U, { 24U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 4U, WLC_OPTIONAL, WLC_U16, 1, 0U, 1U, offsetof(numbers_t, u16), offsetof(numbers_t, has_u16), 0, 0, sizeof(uint16_t), 0U, 0U, { 32U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 5U, WLC_OPTIONAL, WLC_I32, 1, 0U, 1U, offsetof(numbers_t, i32), offsetof(numbers_t, has_i32), 0, 0, sizeof(int32_t), 0U, 0U, { 40U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 6U, WLC_OPTIONAL, WLC_U32, 1, 0U, 1U, offsetof(numbers_t, u32), offsetof(numbers_t, has_u32), 0, 0, sizeof(uint32_t), 0U, 0U, { 48U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 7U, WLC_OPTIONAL, WLC_I64, 1, 0U, 1U, offsetof(numbers_t, i64), offsetof(numbers_t, has_i64), 0, 0, sizeof(int64_t), 0U, 0U, { 56U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 8U, WLC_OPTIONAL, WLC_U64, 1, 0U, 1U, offsetof(numbers_t, u64), offsetof(numbers_t, has_u64), 0, 0, sizeof(uint64_t), 0U, 0U, { 64U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 9U, WLC_OPTIONAL, WLC_F32, 1, 5U, 1U, offsetof(numbers_t, f32), offsetof(numbers_t, has_f32), 0, 0, sizeof(uint32_t), 0U, 0U, { 77U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 10U, WLC_OPTIONAL, WLC_F64, 1, 1U, 1U, offsetof(numbers_t, f64), offsetof(numbers_t, has_f64), 0, 0, sizeof(uint64_t), 0U, 0U, { 81U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 11U, WLC_OPTIONAL, WLC_FLOAT32, 1, 5U, 1U, offsetof(numbers_t, single), offsetof(numbers_t, has_single), 0, 0, sizeof(float), 0U, 0U, { 93U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 12U, WLC_OPTIONAL, WLC_FLOAT64, 1, 1U, 1U, offsetof(numbers_t, double_), offsetof(numbers_t, has_double_), 0, 0, sizeof(double), 0U, 0U, { 97U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 13U, WLC_PACKED, WLC_FLOAT64, 0, 2U, 1U, offsetof(numbers_t, doubles), offsetof(numbers_t, has_doubles), 0, 0, sizeof(double), 2U, 0U, { 106U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 14U, WLC_PACKED, WLC_F32, 1, 2U, 1U, offsetof(numbers_t, words), offsetof(numbers_t, has_words), 0, 0, sizeof(uint32_t), 2U, 0U, { 114U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 15U, WLC_OPTIONAL, WLC_I64, 0, 0U, 1U, offsetof(numbers_t, offset), offsetof(numbers_t, has_offset), 0, 0, sizeof(int64_t), 0U, 0U, { 120U, 0U, 0U }, INT64_C(-7), 0ULL, NULL, NULL },
  { 16U, WLC_OPTIONAL, WLC_I32, 0, 0U, 2U, offsetof(numbers_t, minimum), offsetof(numbers_t, has_minimum), 0, 0, sizeof(int32_t), 0U, 0U, { 128U, 1U, 0U }, INT32_C(-2147483648), 0ULL, NULL, NULL },
  { 17U, WLC_OPTIONAL, WLC_BOOL, 0, 0U, 2U, offsetof(numbers_t, flag), offsetof(numbers_t, has_flag), 0, 0, sizeof(bool), 0U, 0U, { 136U, 1U, 0U }, 0, 1ULL, NULL, NULL },
  { 18U, WLC_OPTIONAL, WLC_STRING, 0, 2U, 2U, offsetof(numbers_t, text), offsetof(numbers_t, has_text), 0, 0, sizeof(wl_codec_string_t), 0U, 16U, { 146U, 1U, 0U }, 0, 8ULL, "\x61""\x22""\x40""\x4E""\x41""\x4D""\x45""\x40", NULL },
};
static const wlc_desc_t numbers_desc = { numbers_fields, sizeof(numbers_fields) / sizeof(numbers_fields[0]), WLC_LOOKUP_DENSE };

void info_request_clear(info_request_t *value) { if (value != NULL) wlc_clear(&info_request_desc, value); }
size_t info_request_encoded_size(const info_request_t *value) { size_t size; return wlc_measure(&info_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t info_request_encode(const info_request_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&info_request_desc, value, out, cap, length); }
wl_codec_status_t info_request_decode(const uint8_t *input, size_t length, info_request_t *out) { return wlc_decode(&info_request_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t info_request_wlc_detail_fingerprint(const info_request_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&info_request_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void settings_clear(settings_t *value) { if (value != NULL) wlc_clear(&settings_desc, value); }
size_t settings_encoded_size(const settings_t *value) { size_t size; return wlc_measure(&settings_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t settings_encode(const settings_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&settings_desc, value, out, cap, length); }
wl_codec_status_t settings_decode(const uint8_t *input, size_t length, settings_t *out) { return wlc_decode(&settings_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t settings_wlc_detail_fingerprint(const settings_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&settings_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void info_response_clear(info_response_t *value) { if (value != NULL) wlc_clear(&info_response_desc, value); }
size_t info_response_encoded_size(const info_response_t *value) { size_t size; return wlc_measure(&info_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t info_response_encode(const info_response_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&info_response_desc, value, out, cap, length); }
wl_codec_status_t info_response_decode(const uint8_t *input, size_t length, info_response_t *out) { return wlc_decode(&info_response_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t info_response_wlc_detail_fingerprint(const info_response_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&info_response_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void configure_request_clear(configure_request_t *value) { if (value != NULL) wlc_clear(&configure_request_desc, value); }
size_t configure_request_encoded_size(const configure_request_t *value) { size_t size; return wlc_measure(&configure_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t configure_request_encode(const configure_request_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&configure_request_desc, value, out, cap, length); }
wl_codec_status_t configure_request_decode(const uint8_t *input, size_t length, configure_request_t *out) { return wlc_decode(&configure_request_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t configure_request_wlc_detail_fingerprint(const configure_request_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&configure_request_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void configure_response_clear(configure_response_t *value) { if (value != NULL) wlc_clear(&configure_response_desc, value); }
size_t configure_response_encoded_size(const configure_response_t *value) { size_t size; return wlc_measure(&configure_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t configure_response_encode(const configure_response_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&configure_response_desc, value, out, cap, length); }
wl_codec_status_t configure_response_decode(const uint8_t *input, size_t length, configure_response_t *out) { return wlc_decode(&configure_response_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t configure_response_wlc_detail_fingerprint(const configure_response_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&configure_response_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void numbers_clear(numbers_t *value) { if (value != NULL) wlc_clear(&numbers_desc, value); }
size_t numbers_encoded_size(const numbers_t *value) { size_t size; return wlc_measure(&numbers_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t numbers_encode(const numbers_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&numbers_desc, value, out, cap, length); }
wl_codec_status_t numbers_decode(const uint8_t *input, size_t length, numbers_t *out) { return wlc_decode(&numbers_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t numbers_wlc_detail_fingerprint(const numbers_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&numbers_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

static void info_request_value_defaults(info_request_value_t *out) {
  (void)out;
}

static void info_request_value_copy_fields(const info_request_t *view, info_request_value_t *out) {
  (void)view; (void)out;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void info_request_wlc_detail_value_copy(const info_request_t *view, info_request_value_t *out) {
  memset(out, 0, sizeof(*out));
  info_request_value_copy_fields(view, out);
}

void info_request_value_clear(info_request_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  info_request_value_defaults(value);
}

wl_codec_status_t info_request_value_from_view(const info_request_t *view, info_request_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&info_request_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  info_request_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t info_request_value_borrow(const info_request_value_t *value, info_request_t *out) {
  info_request_clear(out);
  (void)value;
  return WL_CODEC_OK;
}

wl_codec_status_t info_request_value_to_view(const info_request_value_t *value, info_request_t *out) {
  info_request_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = info_request_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&info_request_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t info_request_value_encoded_size(const info_request_value_t *value) {
  info_request_t view;
  if (value == NULL || info_request_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return info_request_encoded_size(&view);
}

wl_codec_status_t info_request_value_encode(const info_request_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  info_request_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = info_request_value_borrow(value, &view);
  return status == WL_CODEC_OK ? info_request_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t info_request_value_decode(const uint8_t *input, size_t length, info_request_value_t *out) {
  info_request_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = info_request_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  info_request_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void settings_value_defaults(settings_value_t *out) {
  (void)out;
  out->label.length = 6U;
  memcpy(out->label.data, "\xE8""\xAE""\xBE""\xE5""\xA4""\x87", 6U);
  out->enabled = 0;
  out->mode = INT32_C(1);
  out->floor = INT64_MIN;
  out->ceiling = UINT64_C(18446744073709551615);
  out->timeout = 0;
}

static void settings_value_copy_fields(const settings_t *view, settings_value_t *out) {
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
void settings_wlc_detail_value_copy(const settings_t *view, settings_value_t *out) {
  memset(out, 0, sizeof(*out));
  settings_value_copy_fields(view, out);
}

void settings_value_clear(settings_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  settings_value_defaults(value);
}

wl_codec_status_t settings_value_from_view(const settings_t *view, settings_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&settings_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  settings_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t settings_value_borrow(const settings_value_t *value, settings_t *out) {
  settings_clear(out);
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

wl_codec_status_t settings_value_to_view(const settings_value_t *value, settings_t *out) {
  settings_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = settings_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&settings_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t settings_value_encoded_size(const settings_value_t *value) {
  settings_t view;
  if (value == NULL || settings_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return settings_encoded_size(&view);
}

wl_codec_status_t settings_value_encode(const settings_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  settings_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = settings_value_borrow(value, &view);
  return status == WL_CODEC_OK ? settings_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t settings_value_decode(const uint8_t *input, size_t length, settings_value_t *out) {
  settings_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = settings_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  settings_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void info_response_value_defaults(info_response_value_t *out) {
  (void)out;
  settings_value_defaults(&out->settings);
}

static void info_response_value_copy_fields(const info_response_t *view, info_response_value_t *out) {
  out->has_name = view->has_name;
  if (view->has_name) {
    out->name.length = view->name.length;
    if (view->name.length != 0U) memcpy(out->name.data, view->name.data, view->name.length);
  } else {
  }
  out->has_settings = view->has_settings;
  if (view->has_settings) settings_value_copy_fields(&view->settings, &out->settings);
  else settings_value_defaults(&out->settings);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void info_response_wlc_detail_value_copy(const info_response_t *view, info_response_value_t *out) {
  memset(out, 0, sizeof(*out));
  info_response_value_copy_fields(view, out);
}

void info_response_value_clear(info_response_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  info_response_value_defaults(value);
}

wl_codec_status_t info_response_value_from_view(const info_response_t *view, info_response_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&info_response_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  info_response_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t info_response_value_borrow(const info_response_value_t *value, info_response_t *out) {
  info_response_clear(out);
  out->has_name = value->has_name;
  if (value->has_name) {
    if (value->name.length > 32U) return WL_CODEC_ERR_INVALID_VALUE;
    out->name.length = value->name.length;
    out->name.data = value->name.data;
  }
  out->has_settings = value->has_settings;
  if (value->has_settings) {
    wl_codec_status_t status = settings_value_borrow(&value->settings, &out->settings);
    if (status != WL_CODEC_OK) return status;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t info_response_value_to_view(const info_response_value_t *value, info_response_t *out) {
  info_response_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = info_response_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&info_response_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t info_response_value_encoded_size(const info_response_value_t *value) {
  info_response_t view;
  if (value == NULL || info_response_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return info_response_encoded_size(&view);
}

wl_codec_status_t info_response_value_encode(const info_response_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  info_response_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = info_response_value_borrow(value, &view);
  return status == WL_CODEC_OK ? info_response_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t info_response_value_decode(const uint8_t *input, size_t length, info_response_value_t *out) {
  info_response_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = info_response_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  info_response_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void configure_request_value_defaults(configure_request_value_t *out) {
  (void)out;
  settings_value_defaults(&out->settings);
  settings_value_defaults(&out->backup);
}

static void configure_request_value_copy_fields(const configure_request_t *view, configure_request_value_t *out) {
  out->has_settings = view->has_settings;
  if (view->has_settings) settings_value_copy_fields(&view->settings, &out->settings);
  else settings_value_defaults(&out->settings);
  out->has_opaque = view->has_opaque;
  if (view->has_opaque) {
    out->opaque.length = view->opaque.length;
    if (view->opaque.length != 0U) memcpy(out->opaque.data, view->opaque.data, view->opaque.length);
  } else {
  }
  out->has_backup = view->has_backup;
  if (view->has_backup) settings_value_copy_fields(&view->backup, &out->backup);
  else settings_value_defaults(&out->backup);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void configure_request_wlc_detail_value_copy(const configure_request_t *view, configure_request_value_t *out) {
  memset(out, 0, sizeof(*out));
  configure_request_value_copy_fields(view, out);
}

void configure_request_value_clear(configure_request_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  configure_request_value_defaults(value);
}

wl_codec_status_t configure_request_value_from_view(const configure_request_t *view, configure_request_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&configure_request_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  configure_request_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t configure_request_value_borrow(const configure_request_value_t *value, configure_request_t *out) {
  configure_request_clear(out);
  out->has_settings = value->has_settings;
  if (value->has_settings) {
    wl_codec_status_t status = settings_value_borrow(&value->settings, &out->settings);
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
    wl_codec_status_t status = settings_value_borrow(&value->backup, &out->backup);
    if (status != WL_CODEC_OK) return status;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t configure_request_value_to_view(const configure_request_value_t *value, configure_request_t *out) {
  configure_request_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = configure_request_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&configure_request_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t configure_request_value_encoded_size(const configure_request_value_t *value) {
  configure_request_t view;
  if (value == NULL || configure_request_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return configure_request_encoded_size(&view);
}

wl_codec_status_t configure_request_value_encode(const configure_request_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  configure_request_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = configure_request_value_borrow(value, &view);
  return status == WL_CODEC_OK ? configure_request_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t configure_request_value_decode(const uint8_t *input, size_t length, configure_request_value_t *out) {
  configure_request_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = configure_request_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  configure_request_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void configure_response_value_defaults(configure_response_value_t *out) {
  (void)out;
  settings_value_defaults(&out->settings);
  settings_value_defaults(&out->backup);
  out->count = UINT32_C(0);
}

static void configure_response_value_copy_fields(const configure_response_t *view, configure_response_value_t *out) {
  out->has_settings = view->has_settings;
  if (view->has_settings) settings_value_copy_fields(&view->settings, &out->settings);
  else settings_value_defaults(&out->settings);
  out->has_opaque = view->has_opaque;
  if (view->has_opaque) {
    out->opaque.length = view->opaque.length;
    if (view->opaque.length != 0U) memcpy(out->opaque.data, view->opaque.data, view->opaque.length);
  } else {
  }
  out->has_backup = view->has_backup;
  if (view->has_backup) settings_value_copy_fields(&view->backup, &out->backup);
  else settings_value_defaults(&out->backup);
  out->has_count = view->has_count;
  out->count = view->has_count ? view->count : UINT32_C(0);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void configure_response_wlc_detail_value_copy(const configure_response_t *view, configure_response_value_t *out) {
  memset(out, 0, sizeof(*out));
  configure_response_value_copy_fields(view, out);
}

void configure_response_value_clear(configure_response_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  configure_response_value_defaults(value);
}

wl_codec_status_t configure_response_value_from_view(const configure_response_t *view, configure_response_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&configure_response_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  configure_response_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t configure_response_value_borrow(const configure_response_value_t *value, configure_response_t *out) {
  configure_response_clear(out);
  out->has_settings = value->has_settings;
  if (value->has_settings) {
    wl_codec_status_t status = settings_value_borrow(&value->settings, &out->settings);
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
    wl_codec_status_t status = settings_value_borrow(&value->backup, &out->backup);
    if (status != WL_CODEC_OK) return status;
  }
  out->has_count = value->has_count;
  if (value->has_count) {
    out->count = value->count;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t configure_response_value_to_view(const configure_response_value_t *value, configure_response_t *out) {
  configure_response_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = configure_response_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&configure_response_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t configure_response_value_encoded_size(const configure_response_value_t *value) {
  configure_response_t view;
  if (value == NULL || configure_response_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return configure_response_encoded_size(&view);
}

wl_codec_status_t configure_response_value_encode(const configure_response_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  configure_response_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = configure_response_value_borrow(value, &view);
  return status == WL_CODEC_OK ? configure_response_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t configure_response_value_decode(const uint8_t *input, size_t length, configure_response_value_t *out) {
  configure_response_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = configure_response_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  configure_response_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void numbers_value_defaults(numbers_value_t *out) {
  (void)out;
  out->i8 = 0;
  out->u8 = 0;
  out->i16 = 0;
  out->u16 = 0;
  out->i32 = 0;
  out->u32 = UINT32_C(0);
  out->i64 = 0;
  out->u64 = UINT64_C(0);
  out->f32 = UINT32_C(0);
  out->f64 = UINT64_C(0);
  out->single = 0;
  out->double_ = 0;
  out->offset = INT64_C(-7);
  out->minimum = INT32_C(-2147483648);
  out->flag = 1;
  out->text.length = 8U;
  memcpy(out->text.data, "\x61""\x22""\x40""\x4E""\x41""\x4D""\x45""\x40", 8U);
}

static void numbers_value_copy_fields(const numbers_t *view, numbers_value_t *out) {
  out->has_i8 = view->has_i8;
  out->i8 = view->has_i8 ? view->i8 : 0;
  out->has_u8 = view->has_u8;
  out->u8 = view->has_u8 ? view->u8 : 0;
  out->has_i16 = view->has_i16;
  out->i16 = view->has_i16 ? view->i16 : 0;
  out->has_u16 = view->has_u16;
  out->u16 = view->has_u16 ? view->u16 : 0;
  out->has_i32 = view->has_i32;
  out->i32 = view->has_i32 ? view->i32 : 0;
  out->has_u32 = view->has_u32;
  out->u32 = view->has_u32 ? view->u32 : UINT32_C(0);
  out->has_i64 = view->has_i64;
  out->i64 = view->has_i64 ? view->i64 : 0;
  out->has_u64 = view->has_u64;
  out->u64 = view->has_u64 ? view->u64 : UINT64_C(0);
  out->has_f32 = view->has_f32;
  out->f32 = view->has_f32 ? view->f32 : UINT32_C(0);
  out->has_f64 = view->has_f64;
  out->f64 = view->has_f64 ? view->f64 : UINT64_C(0);
  out->has_single = view->has_single;
  out->single = view->has_single ? view->single : 0;
  out->has_double_ = view->has_double_;
  out->double_ = view->has_double_ ? view->double_ : 0;
  out->has_doubles = view->has_doubles;
  if (view->has_doubles) memcpy(out->doubles, view->doubles, sizeof(out->doubles));
  out->has_words = view->has_words;
  if (view->has_words) memcpy(out->words, view->words, sizeof(out->words));
  out->has_offset = view->has_offset;
  out->offset = view->has_offset ? view->offset : INT64_C(-7);
  out->has_minimum = view->has_minimum;
  out->minimum = view->has_minimum ? view->minimum : INT32_C(-2147483648);
  out->has_flag = view->has_flag;
  out->flag = view->has_flag ? view->flag : 1;
  out->has_text = view->has_text;
  if (view->has_text) {
    out->text.length = view->text.length;
    if (view->text.length != 0U) memcpy(out->text.data, view->text.data, view->text.length);
  } else {
  out->text.length = 8U;
  memcpy(out->text.data, "\x61""\x22""\x40""\x4E""\x41""\x4D""\x45""\x40", 8U);
  }
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void numbers_wlc_detail_value_copy(const numbers_t *view, numbers_value_t *out) {
  memset(out, 0, sizeof(*out));
  numbers_value_copy_fields(view, out);
}

void numbers_value_clear(numbers_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  numbers_value_defaults(value);
}

wl_codec_status_t numbers_value_from_view(const numbers_t *view, numbers_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&numbers_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  numbers_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t numbers_value_borrow(const numbers_value_t *value, numbers_t *out) {
  numbers_clear(out);
  out->has_i8 = value->has_i8;
  if (value->has_i8) {
    out->i8 = value->i8;
  }
  out->has_u8 = value->has_u8;
  if (value->has_u8) {
    out->u8 = value->u8;
  }
  out->has_i16 = value->has_i16;
  if (value->has_i16) {
    out->i16 = value->i16;
  }
  out->has_u16 = value->has_u16;
  if (value->has_u16) {
    out->u16 = value->u16;
  }
  out->has_i32 = value->has_i32;
  if (value->has_i32) {
    out->i32 = value->i32;
  }
  out->has_u32 = value->has_u32;
  if (value->has_u32) {
    out->u32 = value->u32;
  }
  out->has_i64 = value->has_i64;
  if (value->has_i64) {
    out->i64 = value->i64;
  }
  out->has_u64 = value->has_u64;
  if (value->has_u64) {
    out->u64 = value->u64;
  }
  out->has_f32 = value->has_f32;
  if (value->has_f32) {
    out->f32 = value->f32;
  }
  out->has_f64 = value->has_f64;
  if (value->has_f64) {
    out->f64 = value->f64;
  }
  out->has_single = value->has_single;
  if (value->has_single) {
    out->single = value->single;
  }
  out->has_double_ = value->has_double_;
  if (value->has_double_) {
    out->double_ = value->double_;
  }
  out->has_doubles = value->has_doubles;
  if (value->has_doubles) {
    memcpy(out->doubles, value->doubles, sizeof(out->doubles));
  }
  out->has_words = value->has_words;
  if (value->has_words) {
    memcpy(out->words, value->words, sizeof(out->words));
  }
  out->has_offset = value->has_offset;
  if (value->has_offset) {
    out->offset = value->offset;
  }
  out->has_minimum = value->has_minimum;
  if (value->has_minimum) {
    out->minimum = value->minimum;
  }
  out->has_flag = value->has_flag;
  if (value->has_flag) {
    out->flag = value->flag;
  }
  out->has_text = value->has_text;
  if (value->has_text) {
    if (value->text.length > 16U) return WL_CODEC_ERR_INVALID_VALUE;
    out->text.length = value->text.length;
    out->text.data = value->text.data;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t numbers_value_to_view(const numbers_value_t *value, numbers_t *out) {
  numbers_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = numbers_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&numbers_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t numbers_value_encoded_size(const numbers_value_t *value) {
  numbers_t view;
  if (value == NULL || numbers_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return numbers_encoded_size(&view);
}

wl_codec_status_t numbers_value_encode(const numbers_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  numbers_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = numbers_value_borrow(value, &view);
  return status == WL_CODEC_OK ? numbers_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t numbers_value_decode(const uint8_t *input, size_t length, numbers_value_t *out) {
  numbers_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = numbers_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  numbers_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

