#include "demo.h"

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
static inline uint8_t wlc_wire(const wlc_field_t *f) {
  return f->wire;
}
static inline uint64_t wlc_z32(int32_t v) {
  return ((uint32_t)v << 1U) ^ (UINT32_C(0) - (uint32_t)(v < 0));
}
static inline int32_t wlc_uz32(uint32_t v) {
  return (int32_t)((v >> 1U) ^ (UINT32_C(0) - (v & 1U)));
}
static inline wl_codec_status_t wlc_measure(const wlc_desc_t *, const void *, size_t *);
static wl_codec_status_t wlc_measure_impl(const wlc_desc_t *, const void *, size_t *, bool);
static void wlc_clear(const wlc_desc_t *, void *);
static inline wl_codec_status_t wlc_decode(const wlc_desc_t *, const uint8_t *, size_t,
                                    void *);
static wl_codec_status_t wlc_emit_fields(const wlc_desc_t *, const void *,
                                         uint8_t **);

static wl_codec_status_t wlc_body(const wlc_field_t *f, const void *p,
                                  size_t *n, bool validated) {
  (void)p;
  (void)validated;
  *n = 0U;
  switch (f->kind) {
    case WLC_I32:
      *n = wlc_vsize(wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    case WLC_BYTES: {
      const wl_codec_bytes_t *v = p;
      if (f->max_length != 0U && v->length > (size_t)f->max_length)
        return WL_CODEC_ERR_INVALID_VALUE;
      if (v->length != 0U && v->data == NULL) return WL_CODEC_ERR_INVALID_VALUE;
      if (wlc_add(n, wlc_vsize(v->length)) != WL_CODEC_OK)
        return WL_CODEC_ERR_OVERFLOW;
      return wlc_add(n, v->length);
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
    size_t count = 1U;
    if (f->card == WLC_OPTIONAL) {
      if (!*(const bool *)(base + f->has)) {
        if (f->required != 0U) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
        continue;
      }
    }
    for (size_t j = 0U; j < count; ++j) {
      size_t body;
      const void *p = base + f->value;
      wl_codec_status_t s = wlc_body(f, p, &body, validated);
      if (s != WL_CODEC_OK) return s;
      if ((s = wlc_add(&n, f->key_size)) != WL_CODEC_OK ||
          (s = wlc_add(&n, body)) != WL_CODEC_OK) return s;
    }
  }
  *out = n;
  return WL_CODEC_OK;
}
static inline wl_codec_status_t wlc_measure(const wlc_desc_t *d, const void *value, size_t *out) {
  return wlc_measure_impl(d, value, out, false);
}
static void wlc_clear(const wlc_desc_t *d, void *value) {
  uint8_t *base = value;
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    void *p = base + f->value;
    (void)p;
    *(bool *)(base + f->has) = false;
    switch (f->kind) {
    case WLC_BYTES: *(wl_codec_bytes_t *)p = (wl_codec_bytes_t){NULL, 0U}; break;
    case WLC_I32:
      *(int32_t *)p = (int32_t)f->signed_default; break;
    default: break;
    }
  }
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
#if REQUEST_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if RESPONSE_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if EVENT_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
static wl_codec_status_t wlc_emit_value(const wlc_field_t *f, const void *p,
                                        uint8_t **out) {
  (void)p; (void)out;
  switch (f->kind) {
    case WLC_I32:
      wlc_putv(out, wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    case WLC_BYTES: {
      const wl_codec_bytes_t *v = p;
      wlc_putv(out, v->length);
      wlc_copy_span(out, v->data, v->length);
      return WL_CODEC_OK;
    }
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
static inline wl_codec_status_t wlc_emit_fields(const wlc_desc_t *d, const void *value,
                                         uint8_t **out) {
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
    size_t count = f->card == WLC_OPTIONAL && *(const bool *)(base + f->has) ? 1U : 0U;
    for (size_t j = 0U; j < count; ++j) {
      const void *p = base + f->value;
      wl_codec_status_t s;
      wlc_put_key(out, f);
      if ((s = wlc_emit_value(f, p, out)) != WL_CODEC_OK) return s;
    }
  }
  return WL_CODEC_OK;
}

static inline wl_codec_status_t wlc_encode(const wlc_desc_t *d, const void *value,
                                    uint8_t *out, size_t cap, size_t *length) {
  size_t n = 0U;
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
static wl_codec_status_t wlc_read_value(const wlc_field_t *f, const uint8_t *in,
                                        size_t n, size_t *at, void *out) {
  (void)in; (void)n; (void)at; (void)out;
  switch (f->kind) {
    case WLC_BYTES:
      {
      uint64_t v;
      wl_codec_status_t s = wlc_getv(in, n, at, &v);
      if (s != WL_CODEC_OK) return s;
      if ((f->kind == WLC_BYTES || f->kind == WLC_STRING) &&
          f->max_length != 0U && v > (uint64_t)f->max_length)
        return WL_CODEC_ERR_INVALID_VALUE;
      if (v > n - *at) return WL_CODEC_ERR_MALFORMED;
      size_t bytes = (size_t)v;
      if (f->kind == WLC_BYTES)
        *(wl_codec_bytes_t *)out = (wl_codec_bytes_t){in + *at, bytes};
      *at += bytes;
      return WL_CODEC_OK;
    }
    default: break;
  }
  uint64_t v;
  wl_codec_status_t s = wlc_getv(in, n, at, &v);
  if (s != WL_CODEC_OK) return s;
  switch (f->kind) {
    case WLC_I32:

      if (v > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
      *(int32_t *)out = wlc_uz32((uint32_t)v); return WL_CODEC_OK;
    default: break;
  }
  return WL_CODEC_ERR_INVALID_VALUE;
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
static inline wl_codec_status_t wlc_decode(const wlc_desc_t *d, const uint8_t *in,
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
    void *p = base + f->value;
    if (f->card == WLC_OPTIONAL) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
    }
    if ((s = wlc_read_value(f, in, n, &at, p)) != WL_CODEC_OK) return s;
    if (f->card == WLC_OPTIONAL) *(bool *)(base + f->has) = true;
  }
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    if (f->required != 0U && !*(const bool *)((uint8_t *)out + f->has))
      return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  }
  return WL_CODEC_OK;
}
static const wlc_field_t request_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_I32, 1, 0U, 1U, offsetof(request_t, input), offsetof(request_t, has_input), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t request_desc = { request_fields, sizeof(request_fields) / sizeof(request_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t response_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_I32, 1, 0U, 1U, offsetof(response_t, output), offsetof(response_t, has_output), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t response_desc = { response_fields, sizeof(response_fields) / sizeof(response_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t event_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_BYTES, 1, 2U, 1U, offsetof(event_t, data), offsetof(event_t, has_data), 0, 0, sizeof(wl_codec_bytes_t), 0U, 9U, { 10U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t event_desc = { event_fields, sizeof(event_fields) / sizeof(event_fields[0]), WLC_LOOKUP_LINEAR };

/* Schema-specialized scalars; ordinary Schema-v1 TLV bytes. */
static inline wl_codec_status_t wlc_scalar_shape_0_wlc_detail_measure(const void *value, size_t *size) {
  if (value == NULL || size == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  if (!(*(const bool *)((const uint8_t *)value + offsetof(request_t, has_input)))) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  *size = (1U + wlc_vsize(wlc_z32((*(const int32_t *)((const uint8_t *)value + offsetof(request_t, input))))));
  return WL_CODEC_OK;
}

static void wlc_scalar_shape_0_clear(void *value) {
  if (value == NULL) return;
  (*(bool *)((uint8_t *)value + offsetof(request_t, has_input))) = false;
  (*(int32_t *)((uint8_t *)value + offsetof(request_t, input))) = 0;
}

static size_t wlc_scalar_shape_0_encoded_size(const void *value) {
  size_t size;
  return wlc_scalar_shape_0_wlc_detail_measure(value, &size) == WL_CODEC_OK ? size : SIZE_MAX;
}

static wl_codec_status_t wlc_scalar_shape_0_encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  size_t size;
  wl_codec_status_t status = wlc_scalar_shape_0_wlc_detail_measure(value, &size);
  if (status != WL_CODEC_OK) return status;
  if (length == NULL || (size != 0U && out == NULL)) return WL_CODEC_ERR_INVALID_VALUE;
  if (capacity < size) return WL_CODEC_ERR_CAPACITY;
  uint8_t *cursor = out;
  if ((*(const bool *)((const uint8_t *)value + offsetof(request_t, has_input)))) {
    *cursor++ = 8U;
    wlc_putv(&cursor, wlc_z32((*(const int32_t *)((const uint8_t *)value + offsetof(request_t, input)))));
  }
  *length = size;
  return WL_CODEC_OK;
}

static wl_codec_status_t wlc_scalar_shape_0_decode(const uint8_t *input, size_t length, void *out) {
  if (out == NULL || (length != 0U && input == NULL)) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_0_clear(out);
  for (size_t at = 0U; at < length;) {
    uint32_t key;
    wl_codec_status_t status;
    if (input[at] < 128U) key = input[at++];
    else {
      uint64_t wide;
      status = wlc_getv(input, length, &at, &wide);
      if (status != WL_CODEC_OK) return status;
      if (wide > UINT32_C(0x7ffff)) return WL_CODEC_ERR_MALFORMED;
      key = (uint32_t)wide;
    }
    uint32_t number = key >> 3U;
    uint8_t wire = (uint8_t)(key & 7U);
    if (number == 0U || number > 65535U ||
        (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    switch ((uint16_t)number) {
      case 1U: {
        if (wire != 0U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((*(bool *)((uint8_t *)out + offsetof(request_t, has_input)))) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t bits;
        status = wlc_getv(input, length, &at, &bits);
        if (status != WL_CODEC_OK) return status;
        if (bits > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        (*(int32_t *)((uint8_t *)out + offsetof(request_t, input))) = (int32_t)(wlc_uz32((uint32_t)bits));
        (*(bool *)((uint8_t *)out + offsetof(request_t, has_input))) = true;
        break;
      }
      default:
        status = wlc_skip(wire, input, length, &at);
        if (status != WL_CODEC_OK) return status;
        break;
    }
  }
  if (!(*(bool *)((uint8_t *)out + offsetof(request_t, has_input)))) return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  return WL_CODEC_OK;
}

static inline wl_codec_status_t request_wlc_detail_measure(const request_t *value, size_t *size) {
  (void)request_desc;
  return wlc_scalar_shape_0_wlc_detail_measure(value, size);
}
void request_clear(request_t *value) { wlc_scalar_shape_0_clear(value); }
size_t request_encoded_size(const request_t *value) { return wlc_scalar_shape_0_encoded_size(value); }
wl_codec_status_t request_encode(const request_t *value, uint8_t *out, size_t capacity, size_t *length) { return wlc_scalar_shape_0_encode(value, out, capacity, length); }
wl_codec_status_t request_decode(const uint8_t *input, size_t length, request_t *out) { return wlc_scalar_shape_0_decode(input, length, out); }

static inline wl_codec_status_t request_wlc_detail_decode_fields(const uint8_t *input, size_t length, void *out) { return wlc_scalar_shape_0_decode(input, length, out); }
static inline size_t request_wlc_detail_encoded_size_fields(const void *value) { return wlc_scalar_shape_0_encoded_size(value); }
static inline wl_codec_status_t request_wlc_detail_encode_fields(const void *value, uint8_t *out, size_t capacity, size_t *length) { return wlc_scalar_shape_0_encode(value, out, capacity, length); }
static inline wl_codec_status_t response_wlc_detail_measure(const response_t *value, size_t *size) {
  (void)response_desc;
  return wlc_scalar_shape_0_wlc_detail_measure(value, size);
}
void response_clear(response_t *value) { wlc_scalar_shape_0_clear(value); }
size_t response_encoded_size(const response_t *value) { return wlc_scalar_shape_0_encoded_size(value); }
wl_codec_status_t response_encode(const response_t *value, uint8_t *out, size_t capacity, size_t *length) { return wlc_scalar_shape_0_encode(value, out, capacity, length); }
wl_codec_status_t response_decode(const uint8_t *input, size_t length, response_t *out) { return wlc_scalar_shape_0_decode(input, length, out); }

static inline wl_codec_status_t response_wlc_detail_decode_fields(const uint8_t *input, size_t length, void *out) { return wlc_scalar_shape_0_decode(input, length, out); }
static inline size_t response_wlc_detail_encoded_size_fields(const void *value) { return wlc_scalar_shape_0_encoded_size(value); }
static inline wl_codec_status_t response_wlc_detail_encode_fields(const void *value, uint8_t *out, size_t capacity, size_t *length) { return wlc_scalar_shape_0_encode(value, out, capacity, length); }
static inline wl_codec_status_t event_wlc_detail_measure(const event_t *value, size_t *size) { return wlc_measure(&event_desc, value, size); }
void event_clear(event_t *value) { if (value != NULL) wlc_clear(&event_desc, value); }
size_t event_encoded_size(const event_t *value) { size_t size; return wlc_measure(&event_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t event_encode(const event_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&event_desc, value, out, cap, length); }
wl_codec_status_t event_decode(const uint8_t *input, size_t length, event_t *out) { return wlc_decode(&event_desc, input, length, out); }

/* A descriptor embeds the ordinary wire plan as its first member. Owned
 * lengths/data use actual member offsets; no view struct aliases an owned one. */
typedef struct {
  wlc_desc_t wire;
  size_t size;
  void (*defaults)(void *);
} wlc_value_desc_t;
static inline wl_codec_status_t wlc_value_decode(const wlc_desc_t *, const uint8_t *, size_t, void *);
static void wlc_value_clear(const wlc_desc_t *wire, void *out) {
  const wlc_value_desc_t *d = (const wlc_value_desc_t *)wire;
  memset(out, 0, d->size);
  d->defaults(out);
}
static wl_codec_status_t wlc_value_read(const wlc_field_t *f, const uint8_t *in,
    size_t n, size_t *at, void *out) {
  if (f->kind == WLC_BYTES || f->kind == WLC_STRING || f->kind == WLC_MESSAGE) {
    uint64_t length;
    wl_codec_status_t status = wlc_getv(in, n, at, &length);
    if (status != WL_CODEC_OK) return status;
    if ((f->kind == WLC_BYTES || f->kind == WLC_STRING) &&
        f->max_length != 0U && length > f->max_length) return WL_CODEC_ERR_INVALID_VALUE;
    if (length > n - *at) return WL_CODEC_ERR_MALFORMED;
    size_t bytes = (size_t)length;
    {
      uint8_t *data = (uint8_t *)out + f->capacity;
      *(size_t *)out = bytes;
      if (f->string_default != NULL) memset(data, 0, (size_t)f->max_length + 1U);
      if (bytes != 0U) memcpy(data, in + *at, bytes);
      if (f->kind == WLC_STRING) data[bytes] = 0U;
    }
    *at += bytes;
    return WL_CODEC_OK;
  }
  return wlc_read_value(f, in, n, at, out);
}
static inline wl_codec_status_t wlc_value_decode(const wlc_desc_t *d, const uint8_t *in,
                                    size_t n, void *out) {
  if (d == NULL || out == NULL || (n != 0U && in == NULL))
    return WL_CODEC_ERR_INVALID_VALUE;
  wlc_value_clear(d, out);
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
    void *p = base + f->value;
    if (f->card == WLC_OPTIONAL) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
    }
    if ((s = wlc_value_read(f, in, n, &at, p)) != WL_CODEC_OK) return s;
    if (f->card == WLC_OPTIONAL) *(bool *)(base + f->has) = true;
  }
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    if (f->required != 0U && !*(const bool *)((uint8_t *)out + f->has))
      return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  }
  return WL_CODEC_OK;
}
static void request_value_defaults(request_value_t *out) {
  (void)out;
  out->input = 0;
}

static void request_value_decode_defaults(void *out) { request_value_defaults(out); }

static const wlc_field_t request_value_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_I32, 1, 0U, 1U, offsetof(request_value_t, input), offsetof(request_value_t, has_input), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_value_desc_t request_value_desc = { { request_value_fields, sizeof(request_value_fields) / sizeof(request_value_fields[0]), WLC_LOOKUP_LINEAR }, sizeof(request_value_t), request_value_decode_defaults };

static wl_codec_status_t request_value_borrow(const request_value_t *, request_t *);
_Static_assert(offsetof(request_t, input) == offsetof(request_value_t, input), "Scalar owned layout");
_Static_assert(offsetof(request_t, has_input) == offsetof(request_value_t, has_input), "Scalar owned layout");
/* The shared scalar backend initializes each field through its primitive type. */
static inline wl_codec_status_t request_wlc_detail_value_decode(const uint8_t *input, size_t length, request_value_t *out) {
  (void)request_value_desc;
  return request_wlc_detail_decode_fields(input, length, out);
}

static void request_value_copy_fields(const request_t *view, request_value_t *out) {
  out->has_input = view->has_input;
  out->input = view->has_input ? view->input : 0;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void request_wlc_detail_value_copy(const request_t *view, request_value_t *out) {
  memset(out, 0, sizeof(*out));
  request_value_copy_fields(view, out);
}

void request_value_clear(request_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  request_value_defaults(value);
}

wl_codec_status_t request_value_from_view(const request_t *view, request_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = request_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  request_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t request_value_borrow(const request_value_t *value, request_t *out) {
  request_clear(out);
  out->has_input = value->has_input;
  if (value->has_input) {
    out->input = value->input;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t request_value_to_view(const request_value_t *value, request_t *out) {
  request_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = request_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = request_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t request_value_encoded_size(const request_value_t *value) { return request_wlc_detail_encoded_size_fields(value); }

wl_codec_status_t request_value_encode(const request_value_t *value, uint8_t *out, size_t capacity, size_t *length) { return request_wlc_detail_encode_fields(value, out, capacity, length); }

wl_codec_status_t request_value_decode(const uint8_t *input, size_t length, request_value_t *out) {
  request_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  memset(&value, 0, sizeof(value));
  status = request_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

static void response_value_defaults(response_value_t *out) {
  (void)out;
  out->output = 0;
}

static void response_value_decode_defaults(void *out) { response_value_defaults(out); }

static const wlc_field_t response_value_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_I32, 1, 0U, 1U, offsetof(response_value_t, output), offsetof(response_value_t, has_output), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_value_desc_t response_value_desc = { { response_value_fields, sizeof(response_value_fields) / sizeof(response_value_fields[0]), WLC_LOOKUP_LINEAR }, sizeof(response_value_t), response_value_decode_defaults };

static wl_codec_status_t response_value_borrow(const response_value_t *, response_t *);
_Static_assert(offsetof(response_t, output) == offsetof(response_value_t, output), "Scalar owned layout");
_Static_assert(offsetof(response_t, has_output) == offsetof(response_value_t, has_output), "Scalar owned layout");
/* The shared scalar backend initializes each field through its primitive type. */
static inline wl_codec_status_t response_wlc_detail_value_decode(const uint8_t *input, size_t length, response_value_t *out) {
  (void)response_value_desc;
  return response_wlc_detail_decode_fields(input, length, out);
}

static void response_value_copy_fields(const response_t *view, response_value_t *out) {
  out->has_output = view->has_output;
  out->output = view->has_output ? view->output : 0;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void response_wlc_detail_value_copy(const response_t *view, response_value_t *out) {
  memset(out, 0, sizeof(*out));
  response_value_copy_fields(view, out);
}

void response_value_clear(response_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  response_value_defaults(value);
}

wl_codec_status_t response_value_from_view(const response_t *view, response_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = response_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  response_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t response_value_borrow(const response_value_t *value, response_t *out) {
  response_clear(out);
  out->has_output = value->has_output;
  if (value->has_output) {
    out->output = value->output;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t response_value_to_view(const response_value_t *value, response_t *out) {
  response_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = response_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = response_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t response_value_encoded_size(const response_value_t *value) { return response_wlc_detail_encoded_size_fields(value); }

wl_codec_status_t response_value_encode(const response_value_t *value, uint8_t *out, size_t capacity, size_t *length) { return response_wlc_detail_encode_fields(value, out, capacity, length); }

wl_codec_status_t response_value_decode(const uint8_t *input, size_t length, response_value_t *out) {
  response_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  memset(&value, 0, sizeof(value));
  status = response_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

static void event_value_defaults(event_value_t *out) {
  (void)out;
}

static void event_value_decode_defaults(void *out) { event_value_defaults(out); }

static const wlc_field_t event_value_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_BYTES, 1, 2U, 1U, offsetof(event_value_t, data), offsetof(event_value_t, has_data), 0, offsetof(event_value_t, data.data) - offsetof(event_value_t, data), sizeof(wl_codec_bytes_t), 0U, 9U, { 10U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_value_desc_t event_value_desc = { { event_value_fields, sizeof(event_value_fields) / sizeof(event_value_fields[0]), WLC_LOOKUP_LINEAR }, sizeof(event_value_t), event_value_decode_defaults };

static wl_codec_status_t event_value_borrow(const event_value_t *, event_t *);
static inline wl_codec_status_t event_wlc_detail_value_decode(const uint8_t *input, size_t length, event_value_t *out) { return wlc_value_decode(&event_value_desc.wire, input, length, out); }

static void event_value_copy_fields(const event_t *view, event_value_t *out) {
  out->has_data = view->has_data;
  if (view->has_data) {
    out->data.length = view->data.length;
    if (view->data.length != 0U) memcpy(out->data.data, view->data.data, view->data.length);
  } else {
  }
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void event_wlc_detail_value_copy(const event_t *view, event_value_t *out) {
  memset(out, 0, sizeof(*out));
  event_value_copy_fields(view, out);
}

void event_value_clear(event_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  event_value_defaults(value);
}

wl_codec_status_t event_value_from_view(const event_t *view, event_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = event_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  event_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t event_value_borrow(const event_value_t *value, event_t *out) {
  event_clear(out);
  out->has_data = value->has_data;
  if (value->has_data) {
    if (value->data.length > 9U) return WL_CODEC_ERR_INVALID_VALUE;
    out->data.length = value->data.length;
    out->data.data = value->data.data;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t event_value_to_view(const event_value_t *value, event_t *out) {
  event_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = event_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = event_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t event_value_encoded_size(const event_value_t *value) {
  event_t view;
  if (value == NULL || event_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return event_encoded_size(&view);
}

wl_codec_status_t event_value_encode(const event_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  event_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = event_value_borrow(value, &view);
  return status == WL_CODEC_OK ? event_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t event_value_decode(const uint8_t *input, size_t length, event_value_t *out) {
  event_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = event_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}
