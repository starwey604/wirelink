#include "calculator.h"

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
  return ((uint32_t)v << 1U) ^ (uint32_t)-(uint32_t)(v < 0);
}
static inline int32_t wlc_uz32(uint32_t v) {
  return (int32_t)((v >> 1U) ^ (uint32_t)-(v & 1U));
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
static inline wl_codec_status_t wlc_measure(const wlc_desc_t *d, const void *value, size_t *out) {
  return wlc_measure_impl(d, value, out, false);
}
static void wlc_clear(const wlc_desc_t *d, void *value) {
  uint8_t *base = value;
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    void *p = base + f->value;
    (void)p;
    if (f->card == WLC_REPEATED) {
      *(size_t *)(base + f->count) = 0U;
      continue;
    }
    *(bool *)(base + f->has) = false;
    switch (f->kind) {
    case WLC_I32:
      *(int32_t *)p = (int32_t)f->signed_default; break;
    default: break;
    }
  }
}
static inline void wlc_put_key(uint8_t **out, const wlc_field_t *f) {
  *(*out)++ = f->key[0];
  if (f->key_size > 1U) {
    *(*out)++ = f->key[1];
    if (f->key_size > 2U) *(*out)++ = f->key[2];
  }
}
#if ADD_REQUEST_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if ADD_RESPONSE_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
static wl_codec_status_t wlc_emit_value(const wlc_field_t *f, const void *p,
                                        uint8_t **out) {
  (void)p; (void)out;
  switch (f->kind) {
    case WLC_I32:
      wlc_putv(out, wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
static inline wl_codec_status_t wlc_emit_fields(const wlc_desc_t *d, const void *value,
                                         uint8_t **out) {
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
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
static wl_codec_status_t wlc_read_value(const wlc_field_t *f, const uint8_t *in,
                                        size_t n, size_t *at, void *out) {
  (void)in; (void)n; (void)at; (void)out;
  switch (f->kind) {
    default: break;
  }
  uint64_t v;
  wl_codec_status_t s = wlc_getv(in, n, at, &v);
  if (s != WL_CODEC_OK) return s;
  switch (f->kind) {
    case WLC_I32:

      if (v > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
      *(int32_t *)out = wlc_uz32((uint32_t)v); break;
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
  return WL_CODEC_OK;
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
/* Generator-private canonical sink. Only successful decode results enter here.
 * The shared emitter orders known fields, drops unknowns, and preserves presence.
 * Scalar bytes use the ordinary endian/varint writers; no raw-frame hashing. */
typedef struct {
  uint64_t hash;
  size_t length;
  wl_codec_status_t status;
} wlc_hash_state_t;
static inline wl_codec_status_t wlc_hash_emit_fields(const wlc_desc_t *, const void *, wlc_hash_state_t *);
static void wlc_hash_copy_span(wlc_hash_state_t *out, const void *data, size_t length) {
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
static wl_codec_status_t wlc_hash_emit_value(const wlc_field_t *f, const void *p,
                                        wlc_hash_state_t *out) {
  (void)p; (void)out;
  switch (f->kind) {
    case WLC_I32:
      wlc_hash_putv(out, wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
static inline wl_codec_status_t wlc_hash_emit_fields(const wlc_desc_t *d, const void *value,
                                         wlc_hash_state_t *out) {
  for (size_t i = 0U; i < d->count; ++i) {
    const wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
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
static const wlc_desc_t add_request_desc;
static const wlc_desc_t add_response_desc;

static const wlc_field_t add_request_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_I32, 1, 0U, 1U, offsetof(add_request_t, left), offsetof(add_request_t, has_left), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_I32, 1, 0U, 1U, offsetof(add_request_t, right), offsetof(add_request_t, has_right), 0, 0, sizeof(int32_t), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t add_request_desc = { add_request_fields, sizeof(add_request_fields) / sizeof(add_request_fields[0]), WLC_LOOKUP_LINEAR };

static const wlc_field_t add_response_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_I32, 1, 0U, 1U, offsetof(add_response_t, sum), offsetof(add_response_t, has_sum), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_desc_t add_response_desc = { add_response_fields, sizeof(add_response_fields) / sizeof(add_response_fields[0]), WLC_LOOKUP_LINEAR };

static inline wl_codec_status_t add_request_wlc_detail_measure(const add_request_t *value, size_t *size) { return wlc_measure(&add_request_desc, value, size); }
void add_request_clear(add_request_t *value) { if (value != NULL) wlc_clear(&add_request_desc, value); }
size_t add_request_encoded_size(const add_request_t *value) { size_t size; return wlc_measure(&add_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t add_request_encode(const add_request_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&add_request_desc, value, out, cap, length); }
wl_codec_status_t add_request_decode(const uint8_t *input, size_t length, add_request_t *out) { return wlc_decode(&add_request_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t add_request_wlc_detail_fingerprint(const add_request_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&add_request_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

static inline wl_codec_status_t add_response_wlc_detail_measure(const add_response_t *value, size_t *size) { return wlc_measure(&add_response_desc, value, size); }
void add_response_clear(add_response_t *value) { if (value != NULL) wlc_clear(&add_response_desc, value); }
size_t add_response_encoded_size(const add_response_t *value) { size_t size; return wlc_measure(&add_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t add_response_encode(const add_response_t *value, uint8_t *out, size_t cap, size_t *length) { return wlc_encode(&add_response_desc, value, out, cap, length); }
wl_codec_status_t add_response_decode(const uint8_t *input, size_t length, add_response_t *out) { return wlc_decode(&add_response_desc, input, length, out); }

/* Generator-private: value must be the unmodified result of successful decode.
 * hash supplies the caller domain seed; codec has no RPC identity policy. */
wl_codec_status_t add_response_wlc_detail_fingerprint(const add_response_t *value, uint64_t *hash, size_t *length) {
  wlc_hash_state_t state = {*hash, 0U, WL_CODEC_OK};
  wl_codec_status_t status = wlc_hash_emit_fields(&add_response_desc, value, &state);
  if (status != WL_CODEC_OK) return status;
  if (state.status != WL_CODEC_OK) return state.status;
  *hash = state.hash;
  *length = state.length;
  return WL_CODEC_OK;
}

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
static const wlc_value_desc_t add_request_value_desc;
static const wlc_value_desc_t add_response_value_desc;
static void add_request_value_defaults(add_request_value_t *out) {
  (void)out;
  out->left = 0;
  out->right = 0;
}

static void add_request_value_decode_defaults(void *out) { add_request_value_defaults(out); }

static const wlc_field_t add_request_value_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_I32, 1, 0U, 1U, offsetof(add_request_value_t, left), offsetof(add_request_value_t, has_left), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, WLC_OPTIONAL, WLC_I32, 1, 0U, 1U, offsetof(add_request_value_t, right), offsetof(add_request_value_t, has_right), 0, 0, sizeof(int32_t), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_value_desc_t add_request_value_desc = { { add_request_value_fields, sizeof(add_request_value_fields) / sizeof(add_request_value_fields[0]), WLC_LOOKUP_LINEAR }, sizeof(add_request_value_t), add_request_value_decode_defaults };

static wl_codec_status_t add_request_value_borrow(const add_request_value_t *, add_request_t *);
static inline wl_codec_status_t add_request_wlc_detail_value_decode(const uint8_t *input, size_t length, add_request_value_t *out) { return wlc_value_decode(&add_request_value_desc.wire, input, length, out); }

static void add_request_value_copy_fields(const add_request_t *view, add_request_value_t *out) {
  out->has_left = view->has_left;
  out->left = view->has_left ? view->left : 0;
  out->has_right = view->has_right;
  out->right = view->has_right ? view->right : 0;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void add_request_wlc_detail_value_copy(const add_request_t *view, add_request_value_t *out) {
  memset(out, 0, sizeof(*out));
  add_request_value_copy_fields(view, out);
}

void add_request_value_clear(add_request_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  add_request_value_defaults(value);
}

wl_codec_status_t add_request_value_from_view(const add_request_t *view, add_request_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = add_request_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  add_request_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t add_request_value_borrow(const add_request_value_t *value, add_request_t *out) {
  add_request_clear(out);
  out->has_left = value->has_left;
  if (value->has_left) {
    out->left = value->left;
  }
  out->has_right = value->has_right;
  if (value->has_right) {
    out->right = value->right;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t add_request_value_to_view(const add_request_value_t *value, add_request_t *out) {
  add_request_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = add_request_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = add_request_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t add_request_value_encoded_size(const add_request_value_t *value) {
  add_request_t view;
  if (value == NULL || add_request_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return add_request_encoded_size(&view);
}

wl_codec_status_t add_request_value_encode(const add_request_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  add_request_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = add_request_value_borrow(value, &view);
  return status == WL_CODEC_OK ? add_request_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t add_request_value_decode(const uint8_t *input, size_t length, add_request_value_t *out) {
  add_request_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = add_request_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

static void add_response_value_defaults(add_response_value_t *out) {
  (void)out;
  out->sum = 0;
}

static void add_response_value_decode_defaults(void *out) { add_response_value_defaults(out); }

static const wlc_field_t add_response_value_fields[] = {
  { 1U, WLC_OPTIONAL, WLC_I32, 1, 0U, 1U, offsetof(add_response_value_t, sum), offsetof(add_response_value_t, has_sum), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const wlc_value_desc_t add_response_value_desc = { { add_response_value_fields, sizeof(add_response_value_fields) / sizeof(add_response_value_fields[0]), WLC_LOOKUP_LINEAR }, sizeof(add_response_value_t), add_response_value_decode_defaults };

static wl_codec_status_t add_response_value_borrow(const add_response_value_t *, add_response_t *);
static inline wl_codec_status_t add_response_wlc_detail_value_decode(const uint8_t *input, size_t length, add_response_value_t *out) { return wlc_value_decode(&add_response_value_desc.wire, input, length, out); }

static void add_response_value_copy_fields(const add_response_t *view, add_response_value_t *out) {
  out->has_sum = view->has_sum;
  out->sum = view->has_sum ? view->sum : 0;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
void add_response_wlc_detail_value_copy(const add_response_t *view, add_response_value_t *out) {
  memset(out, 0, sizeof(*out));
  add_response_value_copy_fields(view, out);
}

void add_response_value_clear(add_response_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  add_response_value_defaults(value);
}

wl_codec_status_t add_response_value_from_view(const add_response_t *view, add_response_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = add_response_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  add_response_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
static wl_codec_status_t add_response_value_borrow(const add_response_value_t *value, add_response_t *out) {
  add_response_clear(out);
  out->has_sum = value->has_sum;
  if (value->has_sum) {
    out->sum = value->sum;
  }
  return WL_CODEC_OK;
}

wl_codec_status_t add_response_value_to_view(const add_response_value_t *value, add_response_t *out) {
  add_response_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = add_response_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = add_response_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

size_t add_response_value_encoded_size(const add_response_value_t *value) {
  add_response_t view;
  if (value == NULL || add_response_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return add_response_encoded_size(&view);
}

wl_codec_status_t add_response_value_encode(const add_response_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  add_response_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = add_response_value_borrow(value, &view);
  return status == WL_CODEC_OK ? add_response_encode(&view, out, capacity, length) : status;
}

wl_codec_status_t add_response_value_decode(const uint8_t *input, size_t length, add_response_value_t *out) {
  add_response_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = add_response_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

