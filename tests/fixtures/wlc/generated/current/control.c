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
  uint8_t card, kind, required;
  size_t value, has, count, capacity, element, packed_count;
  uint16_t max_length;
  int64_t signed_default;
  uint64_t unsigned_default;
  const char *string_default;
  const wlc_desc_t *nested;
} wlc_field_t;
struct wlc_desc { const wlc_field_t *fields; size_t count; };

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
  if (f->card == WLC_PACKED) return 2U;
  if (f->kind == WLC_F64 || f->kind == WLC_FLOAT64) return 1U;
  if (f->kind == WLC_F32 || f->kind == WLC_FLOAT32) return 5U;
  if (f->kind == WLC_BYTES || f->kind == WLC_STRING || f->kind == WLC_MESSAGE)
    return 2U;
  return 0U;
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
                                  size_t *n) {
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
      if (!wlc_utf8((const uint8_t *)v->data, v->length)) return WL_CODEC_ERR_UTF8;
      if (wlc_add(n, wlc_vsize(v->length)) != WL_CODEC_OK)
        return WL_CODEC_ERR_OVERFLOW;
      return wlc_add(n, v->length);
    }
    case WLC_MESSAGE: {
      size_t child;
      wl_codec_status_t s = wlc_measure(f->nested, p, &child);
      if (s != WL_CODEC_OK) return s;
      *n = wlc_vsize(child);
      return wlc_add(n, child);
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
static wl_codec_status_t wlc_measure(const wlc_desc_t *d, const void *value,
                                     size_t *out) {
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
      if ((s = wlc_add(&n, wlc_vsize(((uint64_t)f->number << 3U) | 2U))) != WL_CODEC_OK ||
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
      wl_codec_status_t s = wlc_body(f, p, &body);
      if (s != WL_CODEC_OK) return s;
      if ((s = wlc_add(&n, wlc_vsize(((uint64_t)f->number << 3U) | wlc_wire(f)))) != WL_CODEC_OK ||
          (s = wlc_add(&n, body)) != WL_CODEC_OK) return s;
    }
  }
  *out = n;
  return WL_CODEC_OK;
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
      if (v->length != 0U) { memcpy(*out, v->data, v->length); *out += v->length; }
      return WL_CODEC_OK;
    }
    case WLC_STRING: {
      const wl_codec_string_t *v = p;
      wlc_putv(out, v->length);
      if (v->length != 0U) { memcpy(*out, v->data, v->length); *out += v->length; }
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
      wlc_putv(out, ((uint64_t)f->number << 3U) | 2U);
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
      wlc_putv(out, ((uint64_t)f->number << 3U) | wlc_wire(f));
      if ((s = wlc_emit_value(f, p, out)) != WL_CODEC_OK) return s;
    }
  }
  return WL_CODEC_OK;
}
static wl_codec_status_t wlc_encode(const wlc_desc_t *d, const void *value,
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
static wl_codec_status_t wlc_decode(const wlc_desc_t *d, const uint8_t *in,
                                    size_t n, void *out) {
  if (d == NULL || out == NULL || (n != 0U && in == NULL))
    return WL_CODEC_ERR_INVALID_VALUE;
  wlc_clear(d, out);
  for (size_t at = 0U; at < n;) {
    uint64_t key;
    wl_codec_status_t s = wlc_getv(in, n, &at, &key);
    if (s != WL_CODEC_OK) return s;
    uint64_t number = key >> 3U;
    uint8_t wire = (uint8_t)(key & 7U);
    if (number == 0U || number > 65535U ||
        (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    const wlc_field_t *f = NULL;
    for (size_t i = 0U; i < d->count; ++i) {
      if (d->fields[i].number == number) { f = &d->fields[i]; break; }
    }
    if (f == NULL) {
      if ((s = wlc_skip(wire, in, n, &at)) != WL_CODEC_OK) return s;
      continue;
    }
    if (wire != wlc_wire(f)) return WL_CODEC_ERR_WIRE_TYPE;
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
  { 1U, WLC_OPTIONAL, WLC_F32, 0, offsetof(joint_command_t, position_bits), offsetof(joint_command_t, has_position_bits), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_F32, 0, offsetof(joint_command_t, velocity_bits), offsetof(joint_command_t, has_velocity_bits), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_F32, 0, offsetof(joint_command_t, torque_bits), offsetof(joint_command_t, has_torque_bits), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 4U, WLC_OPTIONAL, WLC_F32, 0, offsetof(joint_command_t, kp_bits), offsetof(joint_command_t, has_kp_bits), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 5U, WLC_OPTIONAL, WLC_F32, 0, offsetof(joint_command_t, kd_bits), offsetof(joint_command_t, has_kd_bits), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 6U, WLC_OPTIONAL, WLC_ENUM, 0, offsetof(joint_command_t, mode), offsetof(joint_command_t, has_mode), 0, 0, sizeof(joint_mode_t), 0U, 0U, INT32_C(0), 0ULL, NULL, NULL },
};
static const wlc_desc_t joint_command_desc = { joint_command_fields, sizeof(joint_command_fields) / sizeof(joint_command_fields[0]) };

static const wlc_field_t arm_command_fields[] = {
  { 1U, WLC_REPEATED, WLC_MESSAGE, 0, offsetof(arm_command_t, joints), 0, offsetof(arm_command_t, joints_count), offsetof(arm_command_t, joints_capacity), sizeof(joint_command_t), 0U, 0U, 0, 0ULL, NULL, &joint_command_desc },
  { 2U, WLC_OPTIONAL, WLC_U64, 0, offsetof(arm_command_t, sequence), offsetof(arm_command_t, has_sequence), 0, 0, sizeof(uint64_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_STRING, 0, offsetof(arm_command_t, source), offsetof(arm_command_t, has_source), 0, 0, sizeof(wl_codec_string_t), 0U, 0U, 0, 4ULL, "\x68""\x6F""\x73""\x74", NULL },
  { 4U, WLC_OPTIONAL, WLC_BYTES, 0, offsetof(arm_command_t, extension), offsetof(arm_command_t, has_extension), 0, 0, sizeof(wl_codec_bytes_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 5U, WLC_OPTIONAL, WLC_BOOL, 0, offsetof(arm_command_t, enabled), offsetof(arm_command_t, has_enabled), 0, 0, sizeof(bool), 0U, 0U, 0, 1ULL, NULL, NULL },
};
static const wlc_desc_t arm_command_desc = { arm_command_fields, sizeof(arm_command_fields) / sizeof(arm_command_fields[0]) };

static const wlc_field_t arm_mit_command_fields[] = {
  { 1U, WLC_PACKED, WLC_FLOAT32, 0, offsetof(arm_mit_command_t, controls), offsetof(arm_mit_command_t, has_controls), 0, 0, sizeof(float), 30U, 0U, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_U64, 0, offsetof(arm_mit_command_t, sequence), offsetof(arm_mit_command_t, has_sequence), 0, 0, sizeof(uint64_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_FLOAT32, 0, offsetof(arm_mit_command_t, dt_s), offsetof(arm_mit_command_t, has_dt_s), 0, 0, sizeof(float), 0U, 0U, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t arm_mit_command_desc = { arm_mit_command_fields, sizeof(arm_mit_command_fields) / sizeof(arm_mit_command_fields[0]) };

static const wlc_field_t home_request_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_U32, 0, offsetof(home_request_t, operation_id), offsetof(home_request_t, has_operation_id), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_U32, 0, offsetof(home_request_t, joint_mask), offsetof(home_request_t, has_joint_mask), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t home_request_desc = { home_request_fields, sizeof(home_request_fields) / sizeof(home_request_fields[0]) };

static const wlc_field_t home_response_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_U32, 0, offsetof(home_response_t, operation_id), offsetof(home_response_t, has_operation_id), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_ENUM, 0, offsetof(home_response_t, status), offsetof(home_response_t, has_status), 0, 0, sizeof(operation_status_t), 0U, 0U, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t home_response_desc = { home_response_fields, sizeof(home_response_fields) / sizeof(home_response_fields[0]) };

static const wlc_field_t bulk_begin_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_F32, 0, offsetof(bulk_begin_t, transfer_id), offsetof(bulk_begin_t, has_transfer_id), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_F64, 0, offsetof(bulk_begin_t, total_length), offsetof(bulk_begin_t, has_total_length), 0, 0, sizeof(uint64_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_F32, 0, offsetof(bulk_begin_t, requested_chunk_size), offsetof(bulk_begin_t, has_requested_chunk_size), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 4U, WLC_OPTIONAL, WLC_F32, 0, offsetof(bulk_begin_t, object_crc32c), offsetof(bulk_begin_t, has_object_crc32c), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t bulk_begin_desc = { bulk_begin_fields, sizeof(bulk_begin_fields) / sizeof(bulk_begin_fields[0]) };

static const wlc_field_t bulk_chunk_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_F32, 0, offsetof(bulk_chunk_t, transfer_id), offsetof(bulk_chunk_t, has_transfer_id), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_F64, 0, offsetof(bulk_chunk_t, offset), offsetof(bulk_chunk_t, has_offset), 0, 0, sizeof(uint64_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_BYTES, 0, offsetof(bulk_chunk_t, data), offsetof(bulk_chunk_t, has_data), 0, 0, sizeof(wl_codec_bytes_t), 0U, 4096U, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t bulk_chunk_desc = { bulk_chunk_fields, sizeof(bulk_chunk_fields) / sizeof(bulk_chunk_fields[0]) };

static const wlc_field_t bulk_end_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_F32, 0, offsetof(bulk_end_t, transfer_id), offsetof(bulk_end_t, has_transfer_id), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_F64, 0, offsetof(bulk_end_t, total_length), offsetof(bulk_end_t, has_total_length), 0, 0, sizeof(uint64_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_F32, 0, offsetof(bulk_end_t, object_crc32c), offsetof(bulk_end_t, has_object_crc32c), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t bulk_end_desc = { bulk_end_fields, sizeof(bulk_end_fields) / sizeof(bulk_end_fields[0]) };

static const wlc_field_t bulk_abort_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_F32, 0, offsetof(bulk_abort_t, transfer_id), offsetof(bulk_abort_t, has_transfer_id), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_I32, 0, offsetof(bulk_abort_t, reason), offsetof(bulk_abort_t, has_reason), 0, 0, sizeof(int32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t bulk_abort_desc = { bulk_abort_fields, sizeof(bulk_abort_fields) / sizeof(bulk_abort_fields[0]) };

static const wlc_field_t bulk_status_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_F32, 0, offsetof(bulk_status_t, transfer_id), offsetof(bulk_status_t, has_transfer_id), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_ENUM, 0, offsetof(bulk_status_t, phase), offsetof(bulk_status_t, has_phase), 0, 0, sizeof(control_bulk_phase_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 3U, WLC_OPTIONAL, WLC_ENUM, 0, offsetof(bulk_status_t, code), offsetof(bulk_status_t, has_code), 0, 0, sizeof(control_bulk_status_code_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 4U, WLC_OPTIONAL, WLC_F64, 0, offsetof(bulk_status_t, next_offset), offsetof(bulk_status_t, has_next_offset), 0, 0, sizeof(uint64_t), 0U, 0U, 0, 0ULL, NULL, NULL },
  { 5U, WLC_OPTIONAL, WLC_F32, 0, offsetof(bulk_status_t, accepted_chunk_size), offsetof(bulk_status_t, has_accepted_chunk_size), 0, 0, sizeof(uint32_t), 0U, 0U, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t bulk_status_desc = { bulk_status_fields, sizeof(bulk_status_fields) / sizeof(bulk_status_fields[0]) };

void joint_command_clear(joint_command_t *value) { if (value != NULL) wlc_clear(&joint_command_desc, value); }
size_t joint_command_encoded_size(const joint_command_t *value) { size_t size; return wlc_measure(&joint_command_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t joint_command_encode(const joint_command_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&joint_command_desc, value, out, cap, length); }
wl_codec_status_t joint_command_decode(const uint8_t *input, size_t length, joint_command_t *out) { return wlc_decode(&joint_command_desc, input, length, out); }

void arm_command_clear(arm_command_t *value) { if (value != NULL) wlc_clear(&arm_command_desc, value); }
size_t arm_command_encoded_size(const arm_command_t *value) { size_t size; return wlc_measure(&arm_command_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t arm_command_encode(const arm_command_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&arm_command_desc, value, out, cap, length); }
wl_codec_status_t arm_command_decode(const uint8_t *input, size_t length, arm_command_t *out) { return wlc_decode(&arm_command_desc, input, length, out); }

void arm_mit_command_clear(arm_mit_command_t *value) { if (value != NULL) wlc_clear(&arm_mit_command_desc, value); }
size_t arm_mit_command_encoded_size(const arm_mit_command_t *value) { size_t size; return wlc_measure(&arm_mit_command_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t arm_mit_command_encode(const arm_mit_command_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&arm_mit_command_desc, value, out, cap, length); }
wl_codec_status_t arm_mit_command_decode(const uint8_t *input, size_t length, arm_mit_command_t *out) { return wlc_decode(&arm_mit_command_desc, input, length, out); }

void home_request_clear(home_request_t *value) { if (value != NULL) wlc_clear(&home_request_desc, value); }
size_t home_request_encoded_size(const home_request_t *value) { size_t size; return wlc_measure(&home_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t home_request_encode(const home_request_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&home_request_desc, value, out, cap, length); }
wl_codec_status_t home_request_decode(const uint8_t *input, size_t length, home_request_t *out) { return wlc_decode(&home_request_desc, input, length, out); }

void home_response_clear(home_response_t *value) { if (value != NULL) wlc_clear(&home_response_desc, value); }
size_t home_response_encoded_size(const home_response_t *value) { size_t size; return wlc_measure(&home_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t home_response_encode(const home_response_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&home_response_desc, value, out, cap, length); }
wl_codec_status_t home_response_decode(const uint8_t *input, size_t length, home_response_t *out) { return wlc_decode(&home_response_desc, input, length, out); }

void bulk_begin_clear(bulk_begin_t *value) { if (value != NULL) wlc_clear(&bulk_begin_desc, value); }
size_t bulk_begin_encoded_size(const bulk_begin_t *value) { size_t size; return wlc_measure(&bulk_begin_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t bulk_begin_encode(const bulk_begin_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&bulk_begin_desc, value, out, cap, length); }
wl_codec_status_t bulk_begin_decode(const uint8_t *input, size_t length, bulk_begin_t *out) { return wlc_decode(&bulk_begin_desc, input, length, out); }

void bulk_chunk_clear(bulk_chunk_t *value) { if (value != NULL) wlc_clear(&bulk_chunk_desc, value); }
size_t bulk_chunk_encoded_size(const bulk_chunk_t *value) { size_t size; return wlc_measure(&bulk_chunk_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t bulk_chunk_encode(const bulk_chunk_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&bulk_chunk_desc, value, out, cap, length); }
wl_codec_status_t bulk_chunk_decode(const uint8_t *input, size_t length, bulk_chunk_t *out) { return wlc_decode(&bulk_chunk_desc, input, length, out); }

void bulk_end_clear(bulk_end_t *value) { if (value != NULL) wlc_clear(&bulk_end_desc, value); }
size_t bulk_end_encoded_size(const bulk_end_t *value) { size_t size; return wlc_measure(&bulk_end_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t bulk_end_encode(const bulk_end_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&bulk_end_desc, value, out, cap, length); }
wl_codec_status_t bulk_end_decode(const uint8_t *input, size_t length, bulk_end_t *out) { return wlc_decode(&bulk_end_desc, input, length, out); }

void bulk_abort_clear(bulk_abort_t *value) { if (value != NULL) wlc_clear(&bulk_abort_desc, value); }
size_t bulk_abort_encoded_size(const bulk_abort_t *value) { size_t size; return wlc_measure(&bulk_abort_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t bulk_abort_encode(const bulk_abort_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&bulk_abort_desc, value, out, cap, length); }
wl_codec_status_t bulk_abort_decode(const uint8_t *input, size_t length, bulk_abort_t *out) { return wlc_decode(&bulk_abort_desc, input, length, out); }

void bulk_status_clear(bulk_status_t *value) { if (value != NULL) wlc_clear(&bulk_status_desc, value); }
size_t bulk_status_encoded_size(const bulk_status_t *value) { size_t size; return wlc_measure(&bulk_status_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t bulk_status_encode(const bulk_status_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&bulk_status_desc, value, out, cap, length); }
wl_codec_status_t bulk_status_decode(const uint8_t *input, size_t length, bulk_status_t *out) { return wlc_decode(&bulk_status_desc, input, length, out); }

