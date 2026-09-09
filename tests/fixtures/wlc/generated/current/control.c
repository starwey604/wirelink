#include "control.h"

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
static const wlc_desc_t joint_command_desc;
static const wlc_desc_t arm_command_desc;
static const wlc_desc_t arm_mit_command_desc;
static const wlc_desc_t home_request_desc;
static const wlc_desc_t home_response_desc;
static const wlc_desc_t bulk_begin_desc;
static const wlc_desc_t bulk_chunk_desc;
static const wlc_desc_t bulk_end_desc;
static const wlc_desc_t bulk_abort_desc;
static const wlc_desc_t bulk_status_desc;

static const wlc_field_t joint_command_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(joint_command_t, position_bits), offsetof(joint_command_t, has_position_bits), 0, 0, sizeof(uint32_t), 0U, 0U, { 13U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(joint_command_t, velocity_bits), offsetof(joint_command_t, has_velocity_bits), 0, 0, sizeof(uint32_t), 0U, 0U, { 21U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(joint_command_t, torque_bits), offsetof(joint_command_t, has_torque_bits), 0, 0, sizeof(uint32_t), 0U, 0U, { 29U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 4U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(joint_command_t, kp_bits), offsetof(joint_command_t, has_kp_bits), 0, 0, sizeof(uint32_t), 0U, 0U, { 37U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 5U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(joint_command_t, kd_bits), offsetof(joint_command_t, has_kd_bits), 0, 0, sizeof(uint32_t), 0U, 0U, { 45U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 6U, WLC_OPTIONAL, WLC_ENUM, 0, 0U, 1U, offsetof(joint_command_t, mode), offsetof(joint_command_t, has_mode), 0, 0, sizeof(joint_mode_t), 0U, 0U, { 48U, 0U, 0U }, INT32_C(0), 0ULL, NULL, NULL },
};
static const wlc_desc_t joint_command_desc = { joint_command_fields, sizeof(joint_command_fields) / sizeof(joint_command_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t arm_command_fields[] = {
  { 1U, WLC_REPEATED, WLC_MESSAGE, 0, 2U, 1U, offsetof(arm_command_t, joints), 0, offsetof(arm_command_t, joints_count), offsetof(arm_command_t, joints_capacity), sizeof(joint_command_t), 0U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, &joint_command_desc },
  { 2U, WLC_OPTIONAL, WLC_U64, 0, 0U, 1U, offsetof(arm_command_t, sequence), offsetof(arm_command_t, has_sequence), 0, 0, sizeof(uint64_t), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_STRING, 0, 2U, 1U, offsetof(arm_command_t, source), offsetof(arm_command_t, has_source), 0, 0, sizeof(wl_codec_string_t), 0U, 0U, { 26U, 0U, 0U }, 0, 4ULL, "\x68""\x6F""\x73""\x74", NULL },
  { 4U, WLC_OPTIONAL, WLC_BYTES, 0, 2U, 1U, offsetof(arm_command_t, extension), offsetof(arm_command_t, has_extension), 0, 0, sizeof(wl_codec_bytes_t), 0U, 0U, { 34U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 5U, WLC_OPTIONAL, WLC_BOOL, 0, 0U, 1U, offsetof(arm_command_t, enabled), offsetof(arm_command_t, has_enabled), 0, 0, sizeof(bool), 0U, 0U, { 40U, 0U, 0U }, 0, 1ULL, NULL, NULL },
};
static const wlc_desc_t arm_command_desc = { arm_command_fields, sizeof(arm_command_fields) / sizeof(arm_command_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t arm_mit_command_fields[] = {
  { 1U, WLC_PACKED, WLC_FLOAT32, 0, 2U, 1U, offsetof(arm_mit_command_t, controls), offsetof(arm_mit_command_t, has_controls), 0, 0, sizeof(float), 30U, 0U, { 10U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_U64, 0, 0U, 1U, offsetof(arm_mit_command_t, sequence), offsetof(arm_mit_command_t, has_sequence), 0, 0, sizeof(uint64_t), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_FLOAT32, 0, 5U, 1U, offsetof(arm_mit_command_t, dt_s), offsetof(arm_mit_command_t, has_dt_s), 0, 0, sizeof(float), 0U, 0U, { 29U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t arm_mit_command_desc = { arm_mit_command_fields, sizeof(arm_mit_command_fields) / sizeof(arm_mit_command_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t home_request_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_U32, 0, 0U, 1U, offsetof(home_request_t, operation_id), offsetof(home_request_t, has_operation_id), 0, 0, sizeof(uint32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_U32, 0, 0U, 1U, offsetof(home_request_t, joint_mask), offsetof(home_request_t, has_joint_mask), 0, 0, sizeof(uint32_t), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t home_request_desc = { home_request_fields, sizeof(home_request_fields) / sizeof(home_request_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t home_response_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_U32, 0, 0U, 1U, offsetof(home_response_t, operation_id), offsetof(home_response_t, has_operation_id), 0, 0, sizeof(uint32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_ENUM, 0, 0U, 1U, offsetof(home_response_t, status), offsetof(home_response_t, has_status), 0, 0, sizeof(operation_status_t), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t home_response_desc = { home_response_fields, sizeof(home_response_fields) / sizeof(home_response_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t bulk_begin_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(bulk_begin_t, transfer_id), offsetof(bulk_begin_t, has_transfer_id), 0, 0, sizeof(uint32_t), 0U, 0U, { 13U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_F64, 0, 1U, 1U, offsetof(bulk_begin_t, total_length), offsetof(bulk_begin_t, has_total_length), 0, 0, sizeof(uint64_t), 0U, 0U, { 17U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(bulk_begin_t, requested_chunk_size), offsetof(bulk_begin_t, has_requested_chunk_size), 0, 0, sizeof(uint32_t), 0U, 0U, { 29U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 4U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(bulk_begin_t, object_crc32c), offsetof(bulk_begin_t, has_object_crc32c), 0, 0, sizeof(uint32_t), 0U, 0U, { 37U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t bulk_begin_desc = { bulk_begin_fields, sizeof(bulk_begin_fields) / sizeof(bulk_begin_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t bulk_chunk_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(bulk_chunk_t, transfer_id), offsetof(bulk_chunk_t, has_transfer_id), 0, 0, sizeof(uint32_t), 0U, 0U, { 13U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_F64, 0, 1U, 1U, offsetof(bulk_chunk_t, offset), offsetof(bulk_chunk_t, has_offset), 0, 0, sizeof(uint64_t), 0U, 0U, { 17U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_BYTES, 0, 2U, 1U, offsetof(bulk_chunk_t, data), offsetof(bulk_chunk_t, has_data), 0, 0, sizeof(wl_codec_bytes_t), 0U, 4096U, { 26U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t bulk_chunk_desc = { bulk_chunk_fields, sizeof(bulk_chunk_fields) / sizeof(bulk_chunk_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t bulk_end_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(bulk_end_t, transfer_id), offsetof(bulk_end_t, has_transfer_id), 0, 0, sizeof(uint32_t), 0U, 0U, { 13U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_F64, 0, 1U, 1U, offsetof(bulk_end_t, total_length), offsetof(bulk_end_t, has_total_length), 0, 0, sizeof(uint64_t), 0U, 0U, { 17U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(bulk_end_t, object_crc32c), offsetof(bulk_end_t, has_object_crc32c), 0, 0, sizeof(uint32_t), 0U, 0U, { 29U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t bulk_end_desc = { bulk_end_fields, sizeof(bulk_end_fields) / sizeof(bulk_end_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t bulk_abort_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(bulk_abort_t, transfer_id), offsetof(bulk_abort_t, has_transfer_id), 0, 0, sizeof(uint32_t), 0U, 0U, { 13U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_I32, 0, 0U, 1U, offsetof(bulk_abort_t, reason), offsetof(bulk_abort_t, has_reason), 0, 0, sizeof(int32_t), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t bulk_abort_desc = { bulk_abort_fields, sizeof(bulk_abort_fields) / sizeof(bulk_abort_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t bulk_status_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(bulk_status_t, transfer_id), offsetof(bulk_status_t, has_transfer_id), 0, 0, sizeof(uint32_t), 0U, 0U, { 13U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_ENUM, 0, 0U, 1U, offsetof(bulk_status_t, phase), offsetof(bulk_status_t, has_phase), 0, 0, sizeof(control_bulk_phase_t), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_ENUM, 0, 0U, 1U, offsetof(bulk_status_t, code), offsetof(bulk_status_t, has_code), 0, 0, sizeof(control_bulk_status_code_t), 0U, 0U, { 24U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 4U, WLC_OPTIONAL, WLC_F64, 0, 1U, 1U, offsetof(bulk_status_t, next_offset), offsetof(bulk_status_t, has_next_offset), 0, 0, sizeof(uint64_t), 0U, 0U, { 33U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 5U, WLC_OPTIONAL, WLC_F32, 0, 5U, 1U, offsetof(bulk_status_t, accepted_chunk_size), offsetof(bulk_status_t, has_accepted_chunk_size), 0, 0, sizeof(uint32_t), 0U, 0U, { 45U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t bulk_status_desc = { bulk_status_fields, sizeof(bulk_status_fields) / sizeof(bulk_status_fields[0]), WLC_LOOKUP_LINEAR };

void joint_command_clear(joint_command_t *value) { if (value != NULL) wlc_clear(&joint_command_desc, value); }
size_t joint_command_encoded_size(const joint_command_t *value) { size_t size; return wlc_measure(&joint_command_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t joint_command_encode(const joint_command_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&joint_command_desc, value, out, cap, length); }
wl_codec_status_t joint_command_decode(const uint8_t *input, size_t length, joint_command_t *out) { return wlc_decode(&joint_command_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t joint_command_wlc_detail_fingerprint(const joint_command_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&joint_command_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void arm_command_clear(arm_command_t *value) { if (value != NULL) wlc_clear(&arm_command_desc, value); }
size_t arm_command_encoded_size(const arm_command_t *value) { size_t size; return wlc_measure(&arm_command_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t arm_command_encode(const arm_command_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&arm_command_desc, value, out, cap, length); }
wl_codec_status_t arm_command_decode(const uint8_t *input, size_t length, arm_command_t *out) { return wlc_decode(&arm_command_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t arm_command_wlc_detail_fingerprint(const arm_command_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&arm_command_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void arm_mit_command_clear(arm_mit_command_t *value) { if (value != NULL) wlc_clear(&arm_mit_command_desc, value); }
size_t arm_mit_command_encoded_size(const arm_mit_command_t *value) { size_t size; return wlc_measure(&arm_mit_command_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t arm_mit_command_encode(const arm_mit_command_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&arm_mit_command_desc, value, out, cap, length); }
wl_codec_status_t arm_mit_command_decode(const uint8_t *input, size_t length, arm_mit_command_t *out) { return wlc_decode(&arm_mit_command_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t arm_mit_command_wlc_detail_fingerprint(const arm_mit_command_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&arm_mit_command_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void home_request_clear(home_request_t *value) { if (value != NULL) wlc_clear(&home_request_desc, value); }
size_t home_request_encoded_size(const home_request_t *value) { size_t size; return wlc_measure(&home_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t home_request_encode(const home_request_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&home_request_desc, value, out, cap, length); }
wl_codec_status_t home_request_decode(const uint8_t *input, size_t length, home_request_t *out) { return wlc_decode(&home_request_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t home_request_wlc_detail_fingerprint(const home_request_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&home_request_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void home_response_clear(home_response_t *value) { if (value != NULL) wlc_clear(&home_response_desc, value); }
size_t home_response_encoded_size(const home_response_t *value) { size_t size; return wlc_measure(&home_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t home_response_encode(const home_response_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&home_response_desc, value, out, cap, length); }
wl_codec_status_t home_response_decode(const uint8_t *input, size_t length, home_response_t *out) { return wlc_decode(&home_response_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t home_response_wlc_detail_fingerprint(const home_response_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&home_response_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void bulk_begin_clear(bulk_begin_t *value) { if (value != NULL) wlc_clear(&bulk_begin_desc, value); }
size_t bulk_begin_encoded_size(const bulk_begin_t *value) { size_t size; return wlc_measure(&bulk_begin_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t bulk_begin_encode(const bulk_begin_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&bulk_begin_desc, value, out, cap, length); }
wl_codec_status_t bulk_begin_decode(const uint8_t *input, size_t length, bulk_begin_t *out) { return wlc_decode(&bulk_begin_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t bulk_begin_wlc_detail_fingerprint(const bulk_begin_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&bulk_begin_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void bulk_chunk_clear(bulk_chunk_t *value) { if (value != NULL) wlc_clear(&bulk_chunk_desc, value); }
size_t bulk_chunk_encoded_size(const bulk_chunk_t *value) { size_t size; return wlc_measure(&bulk_chunk_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t bulk_chunk_encode(const bulk_chunk_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&bulk_chunk_desc, value, out, cap, length); }
wl_codec_status_t bulk_chunk_decode(const uint8_t *input, size_t length, bulk_chunk_t *out) { return wlc_decode(&bulk_chunk_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t bulk_chunk_wlc_detail_fingerprint(const bulk_chunk_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&bulk_chunk_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void bulk_end_clear(bulk_end_t *value) { if (value != NULL) wlc_clear(&bulk_end_desc, value); }
size_t bulk_end_encoded_size(const bulk_end_t *value) { size_t size; return wlc_measure(&bulk_end_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t bulk_end_encode(const bulk_end_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&bulk_end_desc, value, out, cap, length); }
wl_codec_status_t bulk_end_decode(const uint8_t *input, size_t length, bulk_end_t *out) { return wlc_decode(&bulk_end_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t bulk_end_wlc_detail_fingerprint(const bulk_end_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&bulk_end_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void bulk_abort_clear(bulk_abort_t *value) { if (value != NULL) wlc_clear(&bulk_abort_desc, value); }
size_t bulk_abort_encoded_size(const bulk_abort_t *value) { size_t size; return wlc_measure(&bulk_abort_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t bulk_abort_encode(const bulk_abort_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&bulk_abort_desc, value, out, cap, length); }
wl_codec_status_t bulk_abort_decode(const uint8_t *input, size_t length, bulk_abort_t *out) { return wlc_decode(&bulk_abort_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t bulk_abort_wlc_detail_fingerprint(const bulk_abort_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&bulk_abort_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

void bulk_status_clear(bulk_status_t *value) { if (value != NULL) wlc_clear(&bulk_status_desc, value); }
size_t bulk_status_encoded_size(const bulk_status_t *value) { size_t size; return wlc_measure(&bulk_status_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t bulk_status_encode(const bulk_status_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&bulk_status_desc, value, out, cap, length); }
wl_codec_status_t bulk_status_decode(const uint8_t *input, size_t length, bulk_status_t *out) { return wlc_decode(&bulk_status_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t bulk_status_wlc_detail_fingerprint(const bulk_status_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&bulk_status_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

static void joint_command_value_defaults(joint_command_value_t *out) {
  (void)out;
  out->position_bits = UINT32_C(0);
  out->velocity_bits = UINT32_C(0);
  out->torque_bits = UINT32_C(0);
  out->kp_bits = UINT32_C(0);
  out->kd_bits = UINT32_C(0);
  out->mode = INT32_C(0);
}

static void joint_command_value_copy_fields(const joint_command_t *view, joint_command_value_t *out) {
  out->has_position_bits = view->has_position_bits;
  out->position_bits = view->has_position_bits ? view->position_bits : UINT32_C(0);
  out->has_velocity_bits = view->has_velocity_bits;
  out->velocity_bits = view->has_velocity_bits ? view->velocity_bits : UINT32_C(0);
  out->has_torque_bits = view->has_torque_bits;
  out->torque_bits = view->has_torque_bits ? view->torque_bits : UINT32_C(0);
  out->has_kp_bits = view->has_kp_bits;
  out->kp_bits = view->has_kp_bits ? view->kp_bits : UINT32_C(0);
  out->has_kd_bits = view->has_kd_bits;
  out->kd_bits = view->has_kd_bits ? view->kd_bits : UINT32_C(0);
  out->has_mode = view->has_mode;
  out->mode = view->has_mode ? view->mode : INT32_C(0);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void joint_command_wlc_detail_value_copy(const joint_command_t *view, joint_command_value_t *out) {
  memset(out, 0, sizeof(*out));
  joint_command_value_copy_fields(view, out);
}

void joint_command_value_clear(joint_command_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  joint_command_value_defaults(value);
}

wl_codec_status_t joint_command_value_from_view(const joint_command_t *view, joint_command_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&joint_command_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  joint_command_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t joint_command_value_borrow(const joint_command_value_t *value, joint_command_t *out) {
  joint_command_clear(out);
  out->has_position_bits = value->has_position_bits;
  if (value->has_position_bits) {
    out->position_bits = value->position_bits;
  }
  out->has_velocity_bits = value->has_velocity_bits;
  if (value->has_velocity_bits) {
    out->velocity_bits = value->velocity_bits;
  }
  out->has_torque_bits = value->has_torque_bits;
  if (value->has_torque_bits) {
    out->torque_bits = value->torque_bits;
  }
  out->has_kp_bits = value->has_kp_bits;
  if (value->has_kp_bits) {
    out->kp_bits = value->kp_bits;
  }
  out->has_kd_bits = value->has_kd_bits;
  if (value->has_kd_bits) {
    out->kd_bits = value->kd_bits;
  }
  out->has_mode = value->has_mode;
  if (value->has_mode) {
    out->mode = value->mode;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t joint_command_value_to_view(const joint_command_value_t *value, joint_command_t *out) {
  joint_command_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = joint_command_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&joint_command_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t joint_command_value_encoded_size(const joint_command_value_t *value) {
  joint_command_t view;
  if (value == NULL || joint_command_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return joint_command_encoded_size(&view);
}

wl_codec_status_t joint_command_value_encode(const joint_command_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  joint_command_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = joint_command_value_borrow(value, &view);
  return status == WL_CODEC_OK ? joint_command_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t joint_command_value_decode(const uint8_t *input, size_t length, joint_command_value_t *out) {
  joint_command_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = joint_command_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  joint_command_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void arm_mit_command_value_defaults(arm_mit_command_value_t *out) {
  (void)out;
  out->sequence = UINT64_C(0);
  out->dt_s = 0;
}

static void arm_mit_command_value_copy_fields(const arm_mit_command_t *view, arm_mit_command_value_t *out) {
  out->has_controls = view->has_controls;
  if (view->has_controls) memcpy(out->controls, view->controls, sizeof(out->controls));
  out->has_sequence = view->has_sequence;
  out->sequence = view->has_sequence ? view->sequence : UINT64_C(0);
  out->has_dt_s = view->has_dt_s;
  out->dt_s = view->has_dt_s ? view->dt_s : 0;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void arm_mit_command_wlc_detail_value_copy(const arm_mit_command_t *view, arm_mit_command_value_t *out) {
  memset(out, 0, sizeof(*out));
  arm_mit_command_value_copy_fields(view, out);
}

void arm_mit_command_value_clear(arm_mit_command_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  arm_mit_command_value_defaults(value);
}

wl_codec_status_t arm_mit_command_value_from_view(const arm_mit_command_t *view, arm_mit_command_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&arm_mit_command_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  arm_mit_command_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t arm_mit_command_value_borrow(const arm_mit_command_value_t *value, arm_mit_command_t *out) {
  arm_mit_command_clear(out);
  out->has_controls = value->has_controls;
  if (value->has_controls) {
    memcpy(out->controls, value->controls, sizeof(out->controls));
  }
  out->has_sequence = value->has_sequence;
  if (value->has_sequence) {
    out->sequence = value->sequence;
  }
  out->has_dt_s = value->has_dt_s;
  if (value->has_dt_s) {
    out->dt_s = value->dt_s;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t arm_mit_command_value_to_view(const arm_mit_command_value_t *value, arm_mit_command_t *out) {
  arm_mit_command_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = arm_mit_command_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&arm_mit_command_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t arm_mit_command_value_encoded_size(const arm_mit_command_value_t *value) {
  arm_mit_command_t view;
  if (value == NULL || arm_mit_command_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return arm_mit_command_encoded_size(&view);
}

wl_codec_status_t arm_mit_command_value_encode(const arm_mit_command_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  arm_mit_command_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = arm_mit_command_value_borrow(value, &view);
  return status == WL_CODEC_OK ? arm_mit_command_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t arm_mit_command_value_decode(const uint8_t *input, size_t length, arm_mit_command_value_t *out) {
  arm_mit_command_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = arm_mit_command_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  arm_mit_command_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void home_request_value_defaults(home_request_value_t *out) {
  (void)out;
  out->operation_id = UINT32_C(0);
  out->joint_mask = UINT32_C(0);
}

static void home_request_value_copy_fields(const home_request_t *view, home_request_value_t *out) {
  out->has_operation_id = view->has_operation_id;
  out->operation_id = view->has_operation_id ? view->operation_id : UINT32_C(0);
  out->has_joint_mask = view->has_joint_mask;
  out->joint_mask = view->has_joint_mask ? view->joint_mask : UINT32_C(0);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void home_request_wlc_detail_value_copy(const home_request_t *view, home_request_value_t *out) {
  memset(out, 0, sizeof(*out));
  home_request_value_copy_fields(view, out);
}

void home_request_value_clear(home_request_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  home_request_value_defaults(value);
}

wl_codec_status_t home_request_value_from_view(const home_request_t *view, home_request_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&home_request_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  home_request_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t home_request_value_borrow(const home_request_value_t *value, home_request_t *out) {
  home_request_clear(out);
  out->has_operation_id = value->has_operation_id;
  if (value->has_operation_id) {
    out->operation_id = value->operation_id;
  }
  out->has_joint_mask = value->has_joint_mask;
  if (value->has_joint_mask) {
    out->joint_mask = value->joint_mask;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t home_request_value_to_view(const home_request_value_t *value, home_request_t *out) {
  home_request_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = home_request_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&home_request_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t home_request_value_encoded_size(const home_request_value_t *value) {
  home_request_t view;
  if (value == NULL || home_request_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return home_request_encoded_size(&view);
}

wl_codec_status_t home_request_value_encode(const home_request_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  home_request_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = home_request_value_borrow(value, &view);
  return status == WL_CODEC_OK ? home_request_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t home_request_value_decode(const uint8_t *input, size_t length, home_request_value_t *out) {
  home_request_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = home_request_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  home_request_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void home_response_value_defaults(home_response_value_t *out) {
  (void)out;
  out->operation_id = UINT32_C(0);
  out->status = 0;
}

static void home_response_value_copy_fields(const home_response_t *view, home_response_value_t *out) {
  out->has_operation_id = view->has_operation_id;
  out->operation_id = view->has_operation_id ? view->operation_id : UINT32_C(0);
  out->has_status = view->has_status;
  out->status = view->has_status ? view->status : 0;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void home_response_wlc_detail_value_copy(const home_response_t *view, home_response_value_t *out) {
  memset(out, 0, sizeof(*out));
  home_response_value_copy_fields(view, out);
}

void home_response_value_clear(home_response_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  home_response_value_defaults(value);
}

wl_codec_status_t home_response_value_from_view(const home_response_t *view, home_response_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&home_response_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  home_response_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t home_response_value_borrow(const home_response_value_t *value, home_response_t *out) {
  home_response_clear(out);
  out->has_operation_id = value->has_operation_id;
  if (value->has_operation_id) {
    out->operation_id = value->operation_id;
  }
  out->has_status = value->has_status;
  if (value->has_status) {
    out->status = value->status;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t home_response_value_to_view(const home_response_value_t *value, home_response_t *out) {
  home_response_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = home_response_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&home_response_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t home_response_value_encoded_size(const home_response_value_t *value) {
  home_response_t view;
  if (value == NULL || home_response_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return home_response_encoded_size(&view);
}

wl_codec_status_t home_response_value_encode(const home_response_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  home_response_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = home_response_value_borrow(value, &view);
  return status == WL_CODEC_OK ? home_response_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t home_response_value_decode(const uint8_t *input, size_t length, home_response_value_t *out) {
  home_response_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = home_response_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  home_response_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void bulk_begin_value_defaults(bulk_begin_value_t *out) {
  (void)out;
  out->transfer_id = UINT32_C(0);
  out->total_length = UINT64_C(0);
  out->requested_chunk_size = UINT32_C(0);
  out->object_crc32c = UINT32_C(0);
}

static void bulk_begin_value_copy_fields(const bulk_begin_t *view, bulk_begin_value_t *out) {
  out->has_transfer_id = view->has_transfer_id;
  out->transfer_id = view->has_transfer_id ? view->transfer_id : UINT32_C(0);
  out->has_total_length = view->has_total_length;
  out->total_length = view->has_total_length ? view->total_length : UINT64_C(0);
  out->has_requested_chunk_size = view->has_requested_chunk_size;
  out->requested_chunk_size = view->has_requested_chunk_size ? view->requested_chunk_size : UINT32_C(0);
  out->has_object_crc32c = view->has_object_crc32c;
  out->object_crc32c = view->has_object_crc32c ? view->object_crc32c : UINT32_C(0);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void bulk_begin_wlc_detail_value_copy(const bulk_begin_t *view, bulk_begin_value_t *out) {
  memset(out, 0, sizeof(*out));
  bulk_begin_value_copy_fields(view, out);
}

void bulk_begin_value_clear(bulk_begin_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  bulk_begin_value_defaults(value);
}

wl_codec_status_t bulk_begin_value_from_view(const bulk_begin_t *view, bulk_begin_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&bulk_begin_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  bulk_begin_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t bulk_begin_value_borrow(const bulk_begin_value_t *value, bulk_begin_t *out) {
  bulk_begin_clear(out);
  out->has_transfer_id = value->has_transfer_id;
  if (value->has_transfer_id) {
    out->transfer_id = value->transfer_id;
  }
  out->has_total_length = value->has_total_length;
  if (value->has_total_length) {
    out->total_length = value->total_length;
  }
  out->has_requested_chunk_size = value->has_requested_chunk_size;
  if (value->has_requested_chunk_size) {
    out->requested_chunk_size = value->requested_chunk_size;
  }
  out->has_object_crc32c = value->has_object_crc32c;
  if (value->has_object_crc32c) {
    out->object_crc32c = value->object_crc32c;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t bulk_begin_value_to_view(const bulk_begin_value_t *value, bulk_begin_t *out) {
  bulk_begin_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_begin_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&bulk_begin_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t bulk_begin_value_encoded_size(const bulk_begin_value_t *value) {
  bulk_begin_t view;
  if (value == NULL || bulk_begin_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return bulk_begin_encoded_size(&view);
}

wl_codec_status_t bulk_begin_value_encode(const bulk_begin_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  bulk_begin_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_begin_value_borrow(value, &view);
  return status == WL_CODEC_OK ? bulk_begin_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t bulk_begin_value_decode(const uint8_t *input, size_t length, bulk_begin_value_t *out) {
  bulk_begin_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_begin_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  bulk_begin_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void bulk_chunk_value_defaults(bulk_chunk_value_t *out) {
  (void)out;
  out->transfer_id = UINT32_C(0);
  out->offset = UINT64_C(0);
}

static void bulk_chunk_value_copy_fields(const bulk_chunk_t *view, bulk_chunk_value_t *out) {
  out->has_transfer_id = view->has_transfer_id;
  out->transfer_id = view->has_transfer_id ? view->transfer_id : UINT32_C(0);
  out->has_offset = view->has_offset;
  out->offset = view->has_offset ? view->offset : UINT64_C(0);
  out->has_data = view->has_data;
  if (view->has_data) {
    out->data.length = view->data.length;
    if (view->data.length != 0U) memcpy(out->data.data, view->data.data, view->data.length);
  } else {
  }
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void bulk_chunk_wlc_detail_value_copy(const bulk_chunk_t *view, bulk_chunk_value_t *out) {
  memset(out, 0, sizeof(*out));
  bulk_chunk_value_copy_fields(view, out);
}

void bulk_chunk_value_clear(bulk_chunk_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  bulk_chunk_value_defaults(value);
}

wl_codec_status_t bulk_chunk_value_from_view(const bulk_chunk_t *view, bulk_chunk_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&bulk_chunk_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  bulk_chunk_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t bulk_chunk_value_borrow(const bulk_chunk_value_t *value, bulk_chunk_t *out) {
  bulk_chunk_clear(out);
  out->has_transfer_id = value->has_transfer_id;
  if (value->has_transfer_id) {
    out->transfer_id = value->transfer_id;
  }
  out->has_offset = value->has_offset;
  if (value->has_offset) {
    out->offset = value->offset;
  }
  out->has_data = value->has_data;
  if (value->has_data) {
    if (value->data.length > 4096U) return WL_CODEC_ERR_INVALID_VALUE;
    out->data.length = value->data.length;
    out->data.data = value->data.data;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t bulk_chunk_value_to_view(const bulk_chunk_value_t *value, bulk_chunk_t *out) {
  bulk_chunk_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_chunk_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&bulk_chunk_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t bulk_chunk_value_encoded_size(const bulk_chunk_value_t *value) {
  bulk_chunk_t view;
  if (value == NULL || bulk_chunk_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return bulk_chunk_encoded_size(&view);
}

wl_codec_status_t bulk_chunk_value_encode(const bulk_chunk_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  bulk_chunk_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_chunk_value_borrow(value, &view);
  return status == WL_CODEC_OK ? bulk_chunk_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t bulk_chunk_value_decode(const uint8_t *input, size_t length, bulk_chunk_value_t *out) {
  bulk_chunk_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_chunk_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  bulk_chunk_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void bulk_end_value_defaults(bulk_end_value_t *out) {
  (void)out;
  out->transfer_id = UINT32_C(0);
  out->total_length = UINT64_C(0);
  out->object_crc32c = UINT32_C(0);
}

static void bulk_end_value_copy_fields(const bulk_end_t *view, bulk_end_value_t *out) {
  out->has_transfer_id = view->has_transfer_id;
  out->transfer_id = view->has_transfer_id ? view->transfer_id : UINT32_C(0);
  out->has_total_length = view->has_total_length;
  out->total_length = view->has_total_length ? view->total_length : UINT64_C(0);
  out->has_object_crc32c = view->has_object_crc32c;
  out->object_crc32c = view->has_object_crc32c ? view->object_crc32c : UINT32_C(0);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void bulk_end_wlc_detail_value_copy(const bulk_end_t *view, bulk_end_value_t *out) {
  memset(out, 0, sizeof(*out));
  bulk_end_value_copy_fields(view, out);
}

void bulk_end_value_clear(bulk_end_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  bulk_end_value_defaults(value);
}

wl_codec_status_t bulk_end_value_from_view(const bulk_end_t *view, bulk_end_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&bulk_end_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  bulk_end_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t bulk_end_value_borrow(const bulk_end_value_t *value, bulk_end_t *out) {
  bulk_end_clear(out);
  out->has_transfer_id = value->has_transfer_id;
  if (value->has_transfer_id) {
    out->transfer_id = value->transfer_id;
  }
  out->has_total_length = value->has_total_length;
  if (value->has_total_length) {
    out->total_length = value->total_length;
  }
  out->has_object_crc32c = value->has_object_crc32c;
  if (value->has_object_crc32c) {
    out->object_crc32c = value->object_crc32c;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t bulk_end_value_to_view(const bulk_end_value_t *value, bulk_end_t *out) {
  bulk_end_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_end_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&bulk_end_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t bulk_end_value_encoded_size(const bulk_end_value_t *value) {
  bulk_end_t view;
  if (value == NULL || bulk_end_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return bulk_end_encoded_size(&view);
}

wl_codec_status_t bulk_end_value_encode(const bulk_end_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  bulk_end_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_end_value_borrow(value, &view);
  return status == WL_CODEC_OK ? bulk_end_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t bulk_end_value_decode(const uint8_t *input, size_t length, bulk_end_value_t *out) {
  bulk_end_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_end_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  bulk_end_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void bulk_abort_value_defaults(bulk_abort_value_t *out) {
  (void)out;
  out->transfer_id = UINT32_C(0);
  out->reason = 0;
}

static void bulk_abort_value_copy_fields(const bulk_abort_t *view, bulk_abort_value_t *out) {
  out->has_transfer_id = view->has_transfer_id;
  out->transfer_id = view->has_transfer_id ? view->transfer_id : UINT32_C(0);
  out->has_reason = view->has_reason;
  out->reason = view->has_reason ? view->reason : 0;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void bulk_abort_wlc_detail_value_copy(const bulk_abort_t *view, bulk_abort_value_t *out) {
  memset(out, 0, sizeof(*out));
  bulk_abort_value_copy_fields(view, out);
}

void bulk_abort_value_clear(bulk_abort_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  bulk_abort_value_defaults(value);
}

wl_codec_status_t bulk_abort_value_from_view(const bulk_abort_t *view, bulk_abort_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&bulk_abort_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  bulk_abort_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t bulk_abort_value_borrow(const bulk_abort_value_t *value, bulk_abort_t *out) {
  bulk_abort_clear(out);
  out->has_transfer_id = value->has_transfer_id;
  if (value->has_transfer_id) {
    out->transfer_id = value->transfer_id;
  }
  out->has_reason = value->has_reason;
  if (value->has_reason) {
    out->reason = value->reason;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t bulk_abort_value_to_view(const bulk_abort_value_t *value, bulk_abort_t *out) {
  bulk_abort_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_abort_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&bulk_abort_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t bulk_abort_value_encoded_size(const bulk_abort_value_t *value) {
  bulk_abort_t view;
  if (value == NULL || bulk_abort_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return bulk_abort_encoded_size(&view);
}

wl_codec_status_t bulk_abort_value_encode(const bulk_abort_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  bulk_abort_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_abort_value_borrow(value, &view);
  return status == WL_CODEC_OK ? bulk_abort_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t bulk_abort_value_decode(const uint8_t *input, size_t length, bulk_abort_value_t *out) {
  bulk_abort_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_abort_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  bulk_abort_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

static void bulk_status_value_defaults(bulk_status_value_t *out) {
  (void)out;
  out->transfer_id = UINT32_C(0);
  out->phase = 0;
  out->code = 0;
  out->next_offset = UINT64_C(0);
  out->accepted_chunk_size = UINT32_C(0);
}

static void bulk_status_value_copy_fields(const bulk_status_t *view, bulk_status_value_t *out) {
  out->has_transfer_id = view->has_transfer_id;
  out->transfer_id = view->has_transfer_id ? view->transfer_id : UINT32_C(0);
  out->has_phase = view->has_phase;
  out->phase = view->has_phase ? view->phase : 0;
  out->has_code = view->has_code;
  out->code = view->has_code ? view->code : 0;
  out->has_next_offset = view->has_next_offset;
  out->next_offset = view->has_next_offset ? view->next_offset : UINT64_C(0);
  out->has_accepted_chunk_size = view->has_accepted_chunk_size;
  out->accepted_chunk_size = view->has_accepted_chunk_size ? view->accepted_chunk_size : UINT32_C(0);
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void bulk_status_wlc_detail_value_copy(const bulk_status_t *view, bulk_status_value_t *out) {
  memset(out, 0, sizeof(*out));
  bulk_status_value_copy_fields(view, out);
}

void bulk_status_value_clear(bulk_status_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  bulk_status_value_defaults(value);
}

wl_codec_status_t bulk_status_value_from_view(const bulk_status_t *view, bulk_status_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = wlc_measure(&bulk_status_desc, view, &size);
  if (status != WL_CODEC_OK) return status;
  bulk_status_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t bulk_status_value_borrow(const bulk_status_value_t *value, bulk_status_t *out) {
  bulk_status_clear(out);
  out->has_transfer_id = value->has_transfer_id;
  if (value->has_transfer_id) {
    out->transfer_id = value->transfer_id;
  }
  out->has_phase = value->has_phase;
  if (value->has_phase) {
    out->phase = value->phase;
  }
  out->has_code = value->has_code;
  if (value->has_code) {
    out->code = value->code;
  }
  out->has_next_offset = value->has_next_offset;
  if (value->has_next_offset) {
    out->next_offset = value->next_offset;
  }
  out->has_accepted_chunk_size = value->has_accepted_chunk_size;
  if (value->has_accepted_chunk_size) {
    out->accepted_chunk_size = value->accepted_chunk_size;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t bulk_status_value_to_view(const bulk_status_value_t *value, bulk_status_t *out) {
  bulk_status_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_status_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = wlc_measure(&bulk_status_desc, &view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t bulk_status_value_encoded_size(const bulk_status_value_t *value) {
  bulk_status_t view;
  if (value == NULL || bulk_status_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return bulk_status_encoded_size(&view);
}

wl_codec_status_t bulk_status_value_encode(const bulk_status_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  bulk_status_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_status_value_borrow(value, &view);
  return status == WL_CODEC_OK ? bulk_status_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t bulk_status_value_decode(const uint8_t *input, size_t length, bulk_status_value_t *out) {
  bulk_status_t view;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = bulk_status_decode(input, length, &view);
  if (status != WL_CODEC_OK) return status;
  bulk_status_wlc_detail_value_copy(&view, out);
  return WL_CODEC_OK;
}

