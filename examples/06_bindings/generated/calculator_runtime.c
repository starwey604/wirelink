/* RPC layout is a compilation setting, independent of schema/wire identity.
 * ELF markers preserve one input section per function. They do not select
 * a memory region or guarantee ordering. See wlc/docs/rpc-layout.md. */
#ifndef WLC_RPC_LAYOUT_ENABLE
#define WLC_RPC_LAYOUT_ENABLE 1
#define WLC_CALCULATOR_RPC_LAYOUT_GLOBAL_DEFAULT
#endif
#ifndef CALCULATOR_RPC_LAYOUT_ENABLE
#define CALCULATOR_RPC_LAYOUT_ENABLE WLC_RPC_LAYOUT_ENABLE
#define WLC_CALCULATOR_RPC_LAYOUT_MODULE_DEFAULT
#endif

/* Check in the preprocessor too: a C enum may be 1 in a static assertion
 * but is an unknown identifier (and therefore 0) in the layout #if. */
#define WLC_CALCULATOR_RPC_LAYOUT_VALUE_0 1
#define WLC_CALCULATOR_RPC_LAYOUT_VALUE_1 1
#define WLC_CALCULATOR_RPC_LAYOUT_VALUE_(value) WLC_CALCULATOR_RPC_LAYOUT_VALUE_##value
#define WLC_CALCULATOR_RPC_LAYOUT_VALUE(value) WLC_CALCULATOR_RPC_LAYOUT_VALUE_(value)
#if !(WLC_CALCULATOR_RPC_LAYOUT_VALUE(WLC_RPC_LAYOUT_ENABLE))
#error "WLC_RPC_LAYOUT_ENABLE must be literal 0 or 1"
#endif
#if !(WLC_CALCULATOR_RPC_LAYOUT_VALUE(CALCULATOR_RPC_LAYOUT_ENABLE))
#error "CALCULATOR_RPC_LAYOUT_ENABLE must be literal 0 or 1"
#endif
#undef WLC_CALCULATOR_RPC_LAYOUT_VALUE
#undef WLC_CALCULATOR_RPC_LAYOUT_VALUE_
#undef WLC_CALCULATOR_RPC_LAYOUT_VALUE_0
#undef WLC_CALCULATOR_RPC_LAYOUT_VALUE_1

#define WLC_CALCULATOR_RPC_LAYOUT_STRING_(value) #value
#define WLC_CALCULATOR_RPC_LAYOUT_STRING(value) WLC_CALCULATOR_RPC_LAYOUT_STRING_(value)
#if defined(__cplusplus)
#define WLC_CALCULATOR_RPC_LAYOUT_ASSERT static_assert
#else
#define WLC_CALCULATOR_RPC_LAYOUT_ASSERT _Static_assert
#endif
/* Check the whole expanded value: token-pasting alone only checks its first
 * token and can accidentally accept expressions such as 4/2. */
WLC_CALCULATOR_RPC_LAYOUT_ASSERT(
    sizeof(WLC_CALCULATOR_RPC_LAYOUT_STRING(WLC_RPC_LAYOUT_ENABLE)) == 2 &&
    (WLC_RPC_LAYOUT_ENABLE == 0 || WLC_RPC_LAYOUT_ENABLE == 1),
    "WLC_RPC_LAYOUT_ENABLE must be literal 0 or 1");
WLC_CALCULATOR_RPC_LAYOUT_ASSERT(
    sizeof(WLC_CALCULATOR_RPC_LAYOUT_STRING(CALCULATOR_RPC_LAYOUT_ENABLE)) == 2 &&
    (CALCULATOR_RPC_LAYOUT_ENABLE == 0 || CALCULATOR_RPC_LAYOUT_ENABLE == 1),
    "CALCULATOR_RPC_LAYOUT_ENABLE must be literal 0 or 1");
#undef WLC_CALCULATOR_RPC_LAYOUT_ASSERT
#undef WLC_CALCULATOR_RPC_LAYOUT_STRING
#undef WLC_CALCULATOR_RPC_LAYOUT_STRING_

#if CALCULATOR_RPC_LAYOUT_ENABLE && defined(__ELF__) && (defined(__GNUC__) || defined(__clang__))
#define WLC_CALCULATOR_RPC_LAYOUT_ACTIVE 1
#ifndef CALCULATOR_RPC_LAYOUT
#define CALCULATOR_RPC_LAYOUT(function) __attribute__((section(".text.wlc.calculator.rpc." #function)))
#define WLC_CALCULATOR_RPC_LAYOUT_DEFAULT
#endif
#else
#define WLC_CALCULATOR_RPC_LAYOUT_ACTIVE 0
#endif

#include "calculator_runtime.h"

#include <string.h>

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_result)
CALCULATOR_RPC_LAYOUT_calculator_runtime_result
#endif
static calculator_runtime_result_t calculator_runtime_result(const wl_event_t *event) {
  calculator_runtime_result_t result = {0};
  result.domain = CALCULATOR_RUNTIME_INVALID_ARGUMENT;
  if (event != NULL) {
    result.message_id = event->message_id;
    result.event_type = event->type;
  }
  return result;
}

_Static_assert(CALCULATOR_SCHEMA_IDENTITY == UINT64_C(0x96A3B1226F6423DF) && CALCULATOR_BINDING_PROFILE_IDENTITY == UINT64_C(0xC3591689765018B0), "Runtime source/header profile mismatch; regenerate the matched artifacts.");

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_result_str)
CALCULATOR_RPC_LAYOUT_calculator_runtime_result_str
#endif
const char *calculator_runtime_result_str(const calculator_runtime_result_t *result) {
  if (result == NULL) return "null result";
  switch (result->domain) {
    case CALCULATOR_RUNTIME_OK: return "ok";
    case CALCULATOR_RUNTIME_NON_RX: return "non-rx event";
    case CALCULATOR_RUNTIME_UNKNOWN_MESSAGE: return "unknown message";
    case CALCULATOR_RUNTIME_MISSING_ROUTE: return "missing route";
    case CALCULATOR_RUNTIME_MISSING_SCRATCH: return "missing scratch";
    case CALCULATOR_RUNTIME_DELIVERY_MISMATCH: return "delivery mismatch";
    case CALCULATOR_RUNTIME_CODEC_ERROR: return "codec error";
    case CALCULATOR_RUNTIME_STORAGE_ERROR: return "storage error";
    case CALCULATOR_RUNTIME_RPC_ERROR: return "rpc error";
    case CALCULATOR_RUNTIME_CORE_ERROR: return "core error";
    case CALCULATOR_RUNTIME_APPLICATION_ERROR: return "application error";
    case CALCULATOR_RUNTIME_INVALID_ARGUMENT: return "invalid argument";
    default: return "unknown runtime result";
  }
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_clear)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_clear
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_clear)
#endif
static inline void calculator_rpc_add_request_clear(add_request_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_encoded_size)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_encoded_size
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_encoded_size)
#endif
static inline size_t calculator_rpc_add_request_encoded_size(const add_request_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_encode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_encode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_encode)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_encode(const add_request_t *, uint8_t *, size_t, size_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_decode)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_decode(const uint8_t *, size_t, add_request_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_clear)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_clear
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_clear)
#endif
static inline void calculator_rpc_add_request_value_clear(add_request_value_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_encoded_size)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_encoded_size
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_encoded_size)
#endif
static inline size_t calculator_rpc_add_request_value_encoded_size(const add_request_value_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_encode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_encode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_encode)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_value_encode(const add_request_value_t *, uint8_t *, size_t, size_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_decode)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_value_decode(const uint8_t *, size_t, add_request_value_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_from_view)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_from_view
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_from_view)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_value_from_view(const add_request_t *, add_request_value_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_to_view)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_to_view
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_to_view)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_value_to_view(const add_request_value_t *, add_request_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_clear)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_clear
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_clear)
#endif
static inline void calculator_rpc_add_response_clear(add_response_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_encoded_size)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_encoded_size
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_encoded_size)
#endif
static inline size_t calculator_rpc_add_response_encoded_size(const add_response_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_encode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_encode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_encode)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_encode(const add_response_t *, uint8_t *, size_t, size_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_decode)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_decode(const uint8_t *, size_t, add_response_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_clear)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_clear
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_clear)
#endif
static inline void calculator_rpc_add_response_value_clear(add_response_value_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_encoded_size)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_encoded_size
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_encoded_size)
#endif
static inline size_t calculator_rpc_add_response_value_encoded_size(const add_response_value_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_encode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_encode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_encode)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_value_encode(const add_response_value_t *, uint8_t *, size_t, size_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_decode)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_value_decode(const uint8_t *, size_t, add_response_value_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_from_view)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_from_view
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_from_view)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_value_from_view(const add_response_t *, add_response_value_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_to_view)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_to_view
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_to_view)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_value_to_view(const add_response_value_t *, add_response_t *);


#include <limits.h>
#include <stddef.h>
#include <string.h>

enum {
  CALCULATOR_RPC_WLC_OPTIONAL,
  CALCULATOR_RPC_WLC_REPEATED,
  CALCULATOR_RPC_WLC_PACKED,
  CALCULATOR_RPC_WLC_BOOL,
  CALCULATOR_RPC_WLC_U8,
  CALCULATOR_RPC_WLC_U16,
  CALCULATOR_RPC_WLC_U32,
  CALCULATOR_RPC_WLC_U64,
  CALCULATOR_RPC_WLC_I8,
  CALCULATOR_RPC_WLC_I16,
  CALCULATOR_RPC_WLC_I32,
  CALCULATOR_RPC_WLC_I64,
  CALCULATOR_RPC_WLC_F32,
  CALCULATOR_RPC_WLC_F64,
  CALCULATOR_RPC_WLC_FLOAT32,
  CALCULATOR_RPC_WLC_FLOAT64,
  CALCULATOR_RPC_WLC_BYTES,
  CALCULATOR_RPC_WLC_STRING,
  CALCULATOR_RPC_WLC_ENUM,
  CALCULATOR_RPC_WLC_MESSAGE
};
typedef struct calculator_rpc_wlc_desc calculator_rpc_wlc_desc_t;
typedef struct {
  uint16_t number;
  uint8_t card, kind, required, wire, key_size;
  size_t value, has, count, capacity, element, packed_count;
  uint16_t max_length;
  uint8_t key[3];
  int64_t signed_default;
  uint64_t unsigned_default;
  const char *string_default;
  const calculator_rpc_wlc_desc_t *nested;
} calculator_rpc_wlc_field_t;
enum { CALCULATOR_RPC_WLC_LOOKUP_LINEAR, CALCULATOR_RPC_WLC_LOOKUP_DENSE, CALCULATOR_RPC_WLC_LOOKUP_BINARY };
struct calculator_rpc_wlc_desc { const calculator_rpc_wlc_field_t *fields; size_t count; uint8_t lookup; };

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_add)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_add
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_add)
#endif
static inline wl_codec_status_t calculator_rpc_wlc_add(size_t *a, size_t b) {
  if (b > SIZE_MAX - *a) return WL_CODEC_ERR_OVERFLOW;
  *a += b;
  return WL_CODEC_OK;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_vsize)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_vsize
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_vsize)
#endif
static inline size_t calculator_rpc_wlc_vsize(uint64_t v) {
  size_t n = 1U;
  while (v >= 128U) { v >>= 7U; ++n; }
  return n;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_putv)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_putv
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_putv)
#endif
static inline void calculator_rpc_wlc_putv(uint8_t **p, uint64_t v) {
  while (v >= 128U) { *(*p)++ = (uint8_t)(v | 128U); v >>= 7U; }
  *(*p)++ = (uint8_t)v;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_getv)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_getv
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_getv)
#endif
static wl_codec_status_t calculator_rpc_wlc_getv(const uint8_t *in, size_t length, size_t *at,
                                  uint64_t *out) {
  size_t start = *at, n = 0U;
  uint64_t v = 0U;
  while (*at < length && n < 10U) {
    uint8_t b = in[(*at)++];
    if (n == 9U && b > 1U) return WL_CODEC_ERR_OVERFLOW;
    v |= (uint64_t)(b & 127U) << (7U * n++);
    if ((b & 128U) == 0U) {
      if (calculator_rpc_wlc_vsize(v) != *at - start) return WL_CODEC_ERR_MALFORMED;
      *out = v;
      return WL_CODEC_OK;
    }
  }
  return *at == length ? WL_CODEC_ERR_MALFORMED : WL_CODEC_ERR_OVERFLOW;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_wire)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_wire
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_wire)
#endif
static inline uint8_t calculator_rpc_wlc_wire(const calculator_rpc_wlc_field_t *f) {
  return f->wire;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_z32)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_z32
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_z32)
#endif
static inline uint64_t calculator_rpc_wlc_z32(int32_t v) {
  return ((uint32_t)v << 1U) ^ (uint32_t)-(uint32_t)(v < 0);
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_uz32)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_uz32
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_uz32)
#endif
static inline int32_t calculator_rpc_wlc_uz32(uint32_t v) {
  return (int32_t)((v >> 1U) ^ (uint32_t)-(v & 1U));
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_measure)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_measure
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_measure)
#endif
static inline wl_codec_status_t calculator_rpc_wlc_measure(const calculator_rpc_wlc_desc_t *, const void *, size_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_measure_impl)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_measure_impl
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_measure_impl)
#endif
static wl_codec_status_t calculator_rpc_wlc_measure_impl(const calculator_rpc_wlc_desc_t *, const void *, size_t *, bool);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_clear)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_clear
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_clear)
#endif
static void calculator_rpc_wlc_clear(const calculator_rpc_wlc_desc_t *, void *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_decode)
#endif
static inline wl_codec_status_t calculator_rpc_wlc_decode(const calculator_rpc_wlc_desc_t *, const uint8_t *, size_t,
                                    void *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_emit_fields)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_emit_fields
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_emit_fields)
#endif
static wl_codec_status_t calculator_rpc_wlc_emit_fields(const calculator_rpc_wlc_desc_t *, const void *,
                                         uint8_t **);

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_body)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_body
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_body)
#endif
static wl_codec_status_t calculator_rpc_wlc_body(const calculator_rpc_wlc_field_t *f, const void *p,
                                  size_t *n, bool validated) {
  (void)p;
  (void)validated;
  *n = 0U;
  switch (f->kind) {
    case CALCULATOR_RPC_WLC_I32:
      *n = calculator_rpc_wlc_vsize(calculator_rpc_wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_measure_impl)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_measure_impl
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_measure_impl)
#endif
static wl_codec_status_t calculator_rpc_wlc_measure_impl(const calculator_rpc_wlc_desc_t *d, const void *value,
                                     size_t *out, bool validated) {
  size_t n = 0U;
  if (d == NULL || value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  for (size_t i = 0U; i < d->count; ++i) {
    const calculator_rpc_wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
    size_t count = 1U;
    if (f->card == CALCULATOR_RPC_WLC_OPTIONAL) {
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
      const void *p = f->card == CALCULATOR_RPC_WLC_REPEATED
                          ? *(const uint8_t *const *)(base + f->value) + j * f->element
                          : base + f->value;
      wl_codec_status_t s = calculator_rpc_wlc_body(f, p, &body, validated);
      if (s != WL_CODEC_OK) return s;
      if ((s = calculator_rpc_wlc_add(&n, f->key_size)) != WL_CODEC_OK ||
          (s = calculator_rpc_wlc_add(&n, body)) != WL_CODEC_OK) return s;
    }
  }
  *out = n;
  return WL_CODEC_OK;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_measure)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_measure
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_measure)
#endif
static inline wl_codec_status_t calculator_rpc_wlc_measure(const calculator_rpc_wlc_desc_t *d, const void *value, size_t *out) {
  return calculator_rpc_wlc_measure_impl(d, value, out, false);
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_clear)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_clear
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_clear)
#endif
static void calculator_rpc_wlc_clear(const calculator_rpc_wlc_desc_t *d, void *value) {
  uint8_t *base = value;
  for (size_t i = 0U; i < d->count; ++i) {
    const calculator_rpc_wlc_field_t *f = &d->fields[i];
    void *p = base + f->value;
    (void)p;
    if (f->card == CALCULATOR_RPC_WLC_REPEATED) {
      *(size_t *)(base + f->count) = 0U;
      continue;
    }
    *(bool *)(base + f->has) = false;
    switch (f->kind) {
    case CALCULATOR_RPC_WLC_I32:
      *(int32_t *)p = (int32_t)f->signed_default; break;
    default: break;
    }
  }
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_put_key)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_put_key
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_put_key)
#endif
static inline void calculator_rpc_wlc_put_key(uint8_t **out, const calculator_rpc_wlc_field_t *f) {
  *(*out)++ = f->key[0];
  if (f->key_size > 1U) {
    *(*out)++ = f->key[1];
    if (f->key_size > 2U) *(*out)++ = f->key[2];
  }
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_emit_value)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_emit_value
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_emit_value)
#endif
static wl_codec_status_t calculator_rpc_wlc_emit_value(const calculator_rpc_wlc_field_t *f, const void *p,
                                        uint8_t **out) {
  (void)p; (void)out;
  switch (f->kind) {
    case CALCULATOR_RPC_WLC_I32:
      calculator_rpc_wlc_putv(out, calculator_rpc_wlc_z32(*(const int32_t *)p)); return WL_CODEC_OK;
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_emit_fields)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_emit_fields
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_emit_fields)
#endif
static inline wl_codec_status_t calculator_rpc_wlc_emit_fields(const calculator_rpc_wlc_desc_t *d, const void *value,
                                         uint8_t **out) {
  for (size_t i = 0U; i < d->count; ++i) {
    const calculator_rpc_wlc_field_t *f = &d->fields[i];
    const uint8_t *base = value;
    size_t count = f->card == CALCULATOR_RPC_WLC_OPTIONAL
                       ? (*(const bool *)(base + f->has) ? 1U : 0U)
                       : *(const size_t *)(base + f->count);
    for (size_t j = 0U; j < count; ++j) {
      const void *p = f->card == CALCULATOR_RPC_WLC_REPEATED
                          ? *(const uint8_t *const *)(base + f->value) + j * f->element
                          : base + f->value;
      wl_codec_status_t s;
      calculator_rpc_wlc_put_key(out, f);
      if ((s = calculator_rpc_wlc_emit_value(f, p, out)) != WL_CODEC_OK) return s;
    }
  }
  return WL_CODEC_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_encode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_encode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_encode)
#endif
static inline wl_codec_status_t calculator_rpc_wlc_encode(const calculator_rpc_wlc_desc_t *d, const void *value,
                                    uint8_t *out, size_t cap, size_t *length) {
  size_t n;
  wl_codec_status_t s = calculator_rpc_wlc_measure(d, value, &n);
  if (s != WL_CODEC_OK || length == NULL || (n != 0U && out == NULL))
    return s == WL_CODEC_OK ? WL_CODEC_ERR_INVALID_VALUE : s;
  if (cap < n) return WL_CODEC_ERR_CAPACITY;
  uint8_t *p = out;
  if ((s = calculator_rpc_wlc_emit_fields(d, value, &p)) != WL_CODEC_OK) return s;
  *length = n;
  return WL_CODEC_OK;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_skip)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_skip
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_skip)
#endif
static wl_codec_status_t calculator_rpc_wlc_skip(uint8_t wire, const uint8_t *in, size_t n,
                                  size_t *at) {
  uint64_t length;
  wl_codec_status_t s;
  if (wire == 0U) return calculator_rpc_wlc_getv(in, n, at, &length);
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
  if ((s = calculator_rpc_wlc_getv(in, n, at, &length)) != WL_CODEC_OK) return s;
  if (length > n - *at) return WL_CODEC_ERR_MALFORMED;
  *at += (size_t)length;
  return WL_CODEC_OK;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_read_value)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_read_value
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_read_value)
#endif
static wl_codec_status_t calculator_rpc_wlc_read_value(const calculator_rpc_wlc_field_t *f, const uint8_t *in,
                                        size_t n, size_t *at, void *out) {
  (void)in; (void)n; (void)at; (void)out;
  switch (f->kind) {
    default: break;
  }
  uint64_t v;
  wl_codec_status_t s = calculator_rpc_wlc_getv(in, n, at, &v);
  if (s != WL_CODEC_OK) return s;
  switch (f->kind) {
    case CALCULATOR_RPC_WLC_I32:

      if (v > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
      *(int32_t *)out = calculator_rpc_wlc_uz32((uint32_t)v); break;
    default: return WL_CODEC_ERR_INVALID_VALUE;
  }
  return WL_CODEC_OK;
  return WL_CODEC_ERR_INVALID_VALUE;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_find_field)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_find_field
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_find_field)
#endif
static const calculator_rpc_wlc_field_t *calculator_rpc_wlc_find_field(const calculator_rpc_wlc_desc_t *d, uint16_t number) {
  if (d->lookup == CALCULATOR_RPC_WLC_LOOKUP_DENSE) {
    size_t index = (size_t)number - (size_t)d->fields[0].number;
    return index < d->count ? &d->fields[index] : NULL;
  }
  if (d->lookup == CALCULATOR_RPC_WLC_LOOKUP_BINARY) {
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
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_decode)
#endif
static inline wl_codec_status_t calculator_rpc_wlc_decode(const calculator_rpc_wlc_desc_t *d, const uint8_t *in,
                                    size_t n, void *out) {
  if (d == NULL || out == NULL || (n != 0U && in == NULL))
    return WL_CODEC_ERR_INVALID_VALUE;
  calculator_rpc_wlc_clear(d, out);
  // A hint only: missing, unknown or reordered fields still use exact lookup.
  // Each recursive decode owns its cursor; empty schemas avoid NULL arithmetic.
  const calculator_rpc_wlc_field_t *next = d->fields;
  const calculator_rpc_wlc_field_t *end = d->count == 0U ? next : next + d->count;
  for (size_t at = 0U; at < n;) {
    uint64_t key;
    wl_codec_status_t s = calculator_rpc_wlc_getv(in, n, &at, &key);
    if (s != WL_CODEC_OK) return s;
    uint64_t raw_number = key >> 3U;
    uint8_t wire = (uint8_t)(key & 7U);
    if (raw_number == 0U || raw_number > 65535U ||
        (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    uint16_t number = (uint16_t)raw_number;
    const calculator_rpc_wlc_field_t *f = next != end && next->number == number
                               ? next : calculator_rpc_wlc_find_field(d, number);
    if (f == NULL) {
      if ((s = calculator_rpc_wlc_skip(wire, in, n, &at)) != WL_CODEC_OK) return s;
      continue;
    }
    if (wire != calculator_rpc_wlc_wire(f)) return WL_CODEC_ERR_WIRE_TYPE;
    next = f->card == CALCULATOR_RPC_WLC_REPEATED ? f : f + 1;
    uint8_t *base = out;
    void *p = base + f->value;
    if (f->card == CALCULATOR_RPC_WLC_OPTIONAL) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
    }
    if ((s = calculator_rpc_wlc_read_value(f, in, n, &at, p)) != WL_CODEC_OK) return s;
    if (f->card == CALCULATOR_RPC_WLC_OPTIONAL) *(bool *)(base + f->has) = true;
  }
  for (size_t i = 0U; i < d->count; ++i) {
    const calculator_rpc_wlc_field_t *f = &d->fields[i];
    if (f->required != 0U && !*(const bool *)((uint8_t *)out + f->has))
      return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  }
  return WL_CODEC_OK;
}
static const calculator_rpc_wlc_desc_t calculator_rpc_add_request_desc;
static const calculator_rpc_wlc_desc_t calculator_rpc_add_response_desc;

static const calculator_rpc_wlc_field_t calculator_rpc_add_request_fields[] = {
  { 1U, CALCULATOR_RPC_WLC_OPTIONAL, CALCULATOR_RPC_WLC_I32, 1, 0U, 1U, offsetof(add_request_t, left), offsetof(add_request_t, has_left), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, CALCULATOR_RPC_WLC_OPTIONAL, CALCULATOR_RPC_WLC_I32, 1, 0U, 1U, offsetof(add_request_t, right), offsetof(add_request_t, has_right), 0, 0, sizeof(int32_t), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const calculator_rpc_wlc_desc_t calculator_rpc_add_request_desc = { calculator_rpc_add_request_fields, sizeof(calculator_rpc_add_request_fields) / sizeof(calculator_rpc_add_request_fields[0]), CALCULATOR_RPC_WLC_LOOKUP_LINEAR };

static const calculator_rpc_wlc_field_t calculator_rpc_add_response_fields[] = {
  { 1U, CALCULATOR_RPC_WLC_OPTIONAL, CALCULATOR_RPC_WLC_I32, 1, 0U, 1U, offsetof(add_response_t, sum), offsetof(add_response_t, has_sum), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const calculator_rpc_wlc_desc_t calculator_rpc_add_response_desc = { calculator_rpc_add_response_fields, sizeof(calculator_rpc_add_response_fields) / sizeof(calculator_rpc_add_response_fields[0]), CALCULATOR_RPC_WLC_LOOKUP_LINEAR };

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_wlc_detail_measure)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_wlc_detail_measure
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_wlc_detail_measure)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_wlc_detail_measure(const add_request_t *value, size_t *size) { return calculator_rpc_wlc_measure(&calculator_rpc_add_request_desc, value, size); }
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_clear)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_clear
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_clear)
#endif
static inline void calculator_rpc_add_request_clear(add_request_t *value) { if (value != NULL) calculator_rpc_wlc_clear(&calculator_rpc_add_request_desc, value); }
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_encoded_size)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_encoded_size
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_encoded_size)
#endif
static inline size_t calculator_rpc_add_request_encoded_size(const add_request_t *value) { size_t size; return calculator_rpc_wlc_measure(&calculator_rpc_add_request_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_encode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_encode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_encode)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_encode(const add_request_t *value, uint8_t *out, size_t cap, size_t *length) { return calculator_rpc_wlc_encode(&calculator_rpc_add_request_desc, value, out, cap, length); }
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_decode)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_decode(const uint8_t *input, size_t length, add_request_t *out) { return calculator_rpc_wlc_decode(&calculator_rpc_add_request_desc, input, length, out); }

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_wlc_detail_measure)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_wlc_detail_measure
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_wlc_detail_measure)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_wlc_detail_measure(const add_response_t *value, size_t *size) { return calculator_rpc_wlc_measure(&calculator_rpc_add_response_desc, value, size); }
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_clear)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_clear
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_clear)
#endif
static inline void calculator_rpc_add_response_clear(add_response_t *value) { if (value != NULL) calculator_rpc_wlc_clear(&calculator_rpc_add_response_desc, value); }
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_encoded_size)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_encoded_size
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_encoded_size)
#endif
static inline size_t calculator_rpc_add_response_encoded_size(const add_response_t *value) { size_t size; return calculator_rpc_wlc_measure(&calculator_rpc_add_response_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_encode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_encode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_encode)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_encode(const add_response_t *value, uint8_t *out, size_t cap, size_t *length) { return calculator_rpc_wlc_encode(&calculator_rpc_add_response_desc, value, out, cap, length); }
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_decode)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_decode(const uint8_t *input, size_t length, add_response_t *out) { return calculator_rpc_wlc_decode(&calculator_rpc_add_response_desc, input, length, out); }

/* A descriptor embeds the ordinary wire plan as its first member. Owned
 * lengths/data use actual member offsets; no view struct aliases an owned one. */
typedef struct {
  calculator_rpc_wlc_desc_t wire;
  size_t size;
  void (*defaults)(void *);
} calculator_rpc_wlc_value_desc_t;
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_value_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_value_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_value_decode)
#endif
static inline wl_codec_status_t calculator_rpc_wlc_value_decode(const calculator_rpc_wlc_desc_t *, const uint8_t *, size_t, void *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_value_clear)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_value_clear
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_value_clear)
#endif
static void calculator_rpc_wlc_value_clear(const calculator_rpc_wlc_desc_t *wire, void *out) {
  const calculator_rpc_wlc_value_desc_t *d = (const calculator_rpc_wlc_value_desc_t *)wire;
  memset(out, 0, d->size);
  d->defaults(out);
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_value_read)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_value_read
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_value_read)
#endif
static wl_codec_status_t calculator_rpc_wlc_value_read(const calculator_rpc_wlc_field_t *f, const uint8_t *in,
    size_t n, size_t *at, void *out) {
  return calculator_rpc_wlc_read_value(f, in, n, at, out);
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_value_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_wlc_value_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_wlc_value_decode)
#endif
static inline wl_codec_status_t calculator_rpc_wlc_value_decode(const calculator_rpc_wlc_desc_t *d, const uint8_t *in,
                                    size_t n, void *out) {
  if (d == NULL || out == NULL || (n != 0U && in == NULL))
    return WL_CODEC_ERR_INVALID_VALUE;
  calculator_rpc_wlc_value_clear(d, out);
  // A hint only: missing, unknown or reordered fields still use exact lookup.
  // Each recursive decode owns its cursor; empty schemas avoid NULL arithmetic.
  const calculator_rpc_wlc_field_t *next = d->fields;
  const calculator_rpc_wlc_field_t *end = d->count == 0U ? next : next + d->count;
  for (size_t at = 0U; at < n;) {
    uint64_t key;
    wl_codec_status_t s = calculator_rpc_wlc_getv(in, n, &at, &key);
    if (s != WL_CODEC_OK) return s;
    uint64_t raw_number = key >> 3U;
    uint8_t wire = (uint8_t)(key & 7U);
    if (raw_number == 0U || raw_number > 65535U ||
        (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    uint16_t number = (uint16_t)raw_number;
    const calculator_rpc_wlc_field_t *f = next != end && next->number == number
                               ? next : calculator_rpc_wlc_find_field(d, number);
    if (f == NULL) {
      if ((s = calculator_rpc_wlc_skip(wire, in, n, &at)) != WL_CODEC_OK) return s;
      continue;
    }
    if (wire != calculator_rpc_wlc_wire(f)) return WL_CODEC_ERR_WIRE_TYPE;
    next = f->card == CALCULATOR_RPC_WLC_REPEATED ? f : f + 1;
    uint8_t *base = out;
    void *p = base + f->value;
    if (f->card == CALCULATOR_RPC_WLC_OPTIONAL) {
      if (*(bool *)(base + f->has)) return WL_CODEC_ERR_DUPLICATE_FIELD;
    }
    if ((s = calculator_rpc_wlc_value_read(f, in, n, &at, p)) != WL_CODEC_OK) return s;
    if (f->card == CALCULATOR_RPC_WLC_OPTIONAL) *(bool *)(base + f->has) = true;
  }
  for (size_t i = 0U; i < d->count; ++i) {
    const calculator_rpc_wlc_field_t *f = &d->fields[i];
    if (f->required != 0U && !*(const bool *)((uint8_t *)out + f->has))
      return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
  }
  return WL_CODEC_OK;
}
static const calculator_rpc_wlc_value_desc_t calculator_rpc_add_request_value_desc;
static const calculator_rpc_wlc_value_desc_t calculator_rpc_add_response_value_desc;
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_defaults)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_defaults
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_defaults)
#endif
static void calculator_rpc_add_request_value_defaults(add_request_value_t *out) {
  (void)out;
  out->left = 0;
  out->right = 0;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_decode_defaults)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_decode_defaults
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_decode_defaults)
#endif
static void calculator_rpc_add_request_value_decode_defaults(void *out) { calculator_rpc_add_request_value_defaults(out); }

static const calculator_rpc_wlc_field_t calculator_rpc_add_request_value_fields[] = {
  { 1U, CALCULATOR_RPC_WLC_OPTIONAL, CALCULATOR_RPC_WLC_I32, 1, 0U, 1U, offsetof(add_request_value_t, left), offsetof(add_request_value_t, has_left), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
  { 2U, CALCULATOR_RPC_WLC_OPTIONAL, CALCULATOR_RPC_WLC_I32, 1, 0U, 1U, offsetof(add_request_value_t, right), offsetof(add_request_value_t, has_right), 0, 0, sizeof(int32_t), 0U, 0U, { 16U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const calculator_rpc_wlc_value_desc_t calculator_rpc_add_request_value_desc = { { calculator_rpc_add_request_value_fields, sizeof(calculator_rpc_add_request_value_fields) / sizeof(calculator_rpc_add_request_value_fields[0]), CALCULATOR_RPC_WLC_LOOKUP_LINEAR }, sizeof(add_request_value_t), calculator_rpc_add_request_value_decode_defaults };

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_borrow)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_borrow
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_borrow)
#endif
static wl_codec_status_t calculator_rpc_add_request_value_borrow(const add_request_value_t *, add_request_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_wlc_detail_value_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_wlc_detail_value_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_wlc_detail_value_decode)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_wlc_detail_value_decode(const uint8_t *input, size_t length, add_request_value_t *out) { return calculator_rpc_wlc_value_decode(&calculator_rpc_add_request_value_desc.wire, input, length, out); }

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_copy_fields)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_copy_fields
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_copy_fields)
#endif
static void calculator_rpc_add_request_value_copy_fields(const add_request_t *view, add_request_value_t *out) {
  out->has_left = view->has_left;
  out->left = view->has_left ? view->left : 0;
  out->has_right = view->has_right;
  out->right = view->has_right ? view->right : 0;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_wlc_detail_value_copy)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_wlc_detail_value_copy
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_wlc_detail_value_copy)
#endif
static inline void calculator_rpc_add_request_wlc_detail_value_copy(const add_request_t *view, add_request_value_t *out) {
  memset(out, 0, sizeof(*out));
  calculator_rpc_add_request_value_copy_fields(view, out);
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_clear)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_clear
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_clear)
#endif
static inline void calculator_rpc_add_request_value_clear(add_request_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  calculator_rpc_add_request_value_defaults(value);
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_from_view)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_from_view
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_from_view)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_value_from_view(const add_request_t *view, add_request_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = calculator_rpc_add_request_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  calculator_rpc_add_request_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_borrow)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_borrow
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_borrow)
#endif
static wl_codec_status_t calculator_rpc_add_request_value_borrow(const add_request_value_t *value, add_request_t *out) {
  calculator_rpc_add_request_clear(out);
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

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_to_view)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_to_view
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_to_view)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_value_to_view(const add_request_value_t *value, add_request_t *out) {
  add_request_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = calculator_rpc_add_request_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = calculator_rpc_add_request_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_encoded_size)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_encoded_size
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_encoded_size)
#endif
static inline size_t calculator_rpc_add_request_value_encoded_size(const add_request_value_t *value) {
  add_request_t view;
  if (value == NULL || calculator_rpc_add_request_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return calculator_rpc_add_request_encoded_size(&view);
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_encode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_encode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_encode)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_value_encode(const add_request_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  add_request_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = calculator_rpc_add_request_value_borrow(value, &view);
  return status == WL_CODEC_OK ? calculator_rpc_add_request_encode(&view, out, capacity, length) : status;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_value_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_value_decode)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_value_decode(const uint8_t *input, size_t length, add_request_value_t *out) {
  add_request_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = calculator_rpc_add_request_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_defaults)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_defaults
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_defaults)
#endif
static void calculator_rpc_add_response_value_defaults(add_response_value_t *out) {
  (void)out;
  out->sum = 0;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_decode_defaults)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_decode_defaults
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_decode_defaults)
#endif
static void calculator_rpc_add_response_value_decode_defaults(void *out) { calculator_rpc_add_response_value_defaults(out); }

static const calculator_rpc_wlc_field_t calculator_rpc_add_response_value_fields[] = {
  { 1U, CALCULATOR_RPC_WLC_OPTIONAL, CALCULATOR_RPC_WLC_I32, 1, 0U, 1U, offsetof(add_response_value_t, sum), offsetof(add_response_value_t, has_sum), 0, 0, sizeof(int32_t), 0U, 0U, { 8U, 0U, 0U }, 0, 0ULL, NULL, NULL },
};
static const calculator_rpc_wlc_value_desc_t calculator_rpc_add_response_value_desc = { { calculator_rpc_add_response_value_fields, sizeof(calculator_rpc_add_response_value_fields) / sizeof(calculator_rpc_add_response_value_fields[0]), CALCULATOR_RPC_WLC_LOOKUP_LINEAR }, sizeof(add_response_value_t), calculator_rpc_add_response_value_decode_defaults };

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_borrow)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_borrow
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_borrow)
#endif
static wl_codec_status_t calculator_rpc_add_response_value_borrow(const add_response_value_t *, add_response_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_wlc_detail_value_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_wlc_detail_value_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_wlc_detail_value_decode)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_wlc_detail_value_decode(const uint8_t *input, size_t length, add_response_value_t *out) { return calculator_rpc_wlc_value_decode(&calculator_rpc_add_response_value_desc.wire, input, length, out); }

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_copy_fields)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_copy_fields
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_copy_fields)
#endif
static void calculator_rpc_add_response_value_copy_fields(const add_response_t *view, add_response_value_t *out) {
  out->has_sum = view->has_sum;
  out->sum = view->has_sum ? view->sum : 0;
}

/* Generator-private: input is an unmodified successful decode, or has been measured. */
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_wlc_detail_value_copy)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_wlc_detail_value_copy
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_wlc_detail_value_copy)
#endif
static inline void calculator_rpc_add_response_wlc_detail_value_copy(const add_response_t *view, add_response_value_t *out) {
  memset(out, 0, sizeof(*out));
  calculator_rpc_add_response_value_copy_fields(view, out);
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_clear)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_clear
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_clear)
#endif
static inline void calculator_rpc_add_response_value_clear(add_response_value_t *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  calculator_rpc_add_response_value_defaults(value);
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_from_view)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_from_view
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_from_view)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_value_from_view(const add_response_t *view, add_response_value_t *out) {
  size_t size;
  wl_codec_status_t status;
  if (view == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = calculator_rpc_add_response_wlc_detail_measure(view, &size);
  if (status != WL_CODEC_OK) return status;
  calculator_rpc_add_response_wlc_detail_value_copy(view, out);
  return WL_CODEC_OK;
}

/* Private conversion does not re-validate each nested subtree. */
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_borrow)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_borrow
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_borrow)
#endif
static wl_codec_status_t calculator_rpc_add_response_value_borrow(const add_response_value_t *value, add_response_t *out) {
  calculator_rpc_add_response_clear(out);
  out->has_sum = value->has_sum;
  if (value->has_sum) {
    out->sum = value->sum;
  }
  return WL_CODEC_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_to_view)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_to_view
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_to_view)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_value_to_view(const add_response_value_t *value, add_response_t *out) {
  add_response_t view;
  size_t size;
  wl_codec_status_t status;
  if (value == NULL || out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = calculator_rpc_add_response_value_borrow(value, &view);
  if (status == WL_CODEC_OK) status = calculator_rpc_add_response_wlc_detail_measure(&view, &size);
  if (status != WL_CODEC_OK) return status;
  *out = view;
  return WL_CODEC_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_encoded_size)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_encoded_size
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_encoded_size)
#endif
static inline size_t calculator_rpc_add_response_value_encoded_size(const add_response_value_t *value) {
  add_response_t view;
  if (value == NULL || calculator_rpc_add_response_value_borrow(value, &view) != WL_CODEC_OK) return SIZE_MAX;
  return calculator_rpc_add_response_encoded_size(&view);
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_encode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_encode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_encode)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_value_encode(const add_response_value_t *value, uint8_t *out, size_t capacity, size_t *length) {
  add_response_t view;
  wl_codec_status_t status;
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = calculator_rpc_add_response_value_borrow(value, &view);
  return status == WL_CODEC_OK ? calculator_rpc_add_response_encode(&view, out, capacity, length) : status;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_decode)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_response_value_decode
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_response_value_decode)
#endif
static inline wl_codec_status_t calculator_rpc_add_response_value_decode(const uint8_t *input, size_t length, add_response_value_t *out) {
  add_response_value_t value;
  wl_codec_status_t status;
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  status = calculator_rpc_add_response_wlc_detail_value_decode(input, length, &value);
  if (status != WL_CODEC_OK) return status;
  *out = value;
  return WL_CODEC_OK;
}

wl_codec_status_t add_request_wlc_detail_fingerprint(const add_request_t *, uint64_t *, size_t *);
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_wlc_detail_fingerprint)
CALCULATOR_RPC_LAYOUT_calculator_rpc_add_request_wlc_detail_fingerprint
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_add_request_wlc_detail_fingerprint)
#endif
static inline wl_codec_status_t calculator_rpc_add_request_wlc_detail_fingerprint(const add_request_t *value, uint64_t *hash, size_t *length) { return add_request_wlc_detail_fingerprint(value, hash, length); }

/* Managed RPC v4: BE64 originating client session, then tagged varuint32
 * correlation. Its first byte reserves bit zero for rejection; the remaining
 * bits encode all 32 operation bits. Native message ID determines direction.
 * Only rejection adds BE32 nonzero status. */
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_read_u32)
CALCULATOR_RPC_LAYOUT_calculator_rpc_read_u32
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_read_u32)
#endif
static uint32_t calculator_rpc_read_u32(const uint8_t *data) {
  return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
         ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_write_u32)
CALCULATOR_RPC_LAYOUT_calculator_rpc_write_u32
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_write_u32)
#endif
static void calculator_rpc_write_u32(uint8_t *data, uint32_t value) {
  data[0] = (uint8_t)(value >> 24U);
  data[1] = (uint8_t)(value >> 16U);
  data[2] = (uint8_t)(value >> 8U);
  data[3] = (uint8_t)value;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_header_size)
CALCULATOR_RPC_LAYOUT_calculator_rpc_header_size
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_header_size)
#endif
static size_t calculator_rpc_header_size(uint32_t operation_id, int32_t status) {
  size_t size = status == 0 ? 9U : 13U;
  operation_id >>= 6U;
  while (operation_id != 0U) { ++size; operation_id >>= 7U; }
  return size;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_header_read)
CALCULATOR_RPC_LAYOUT_calculator_rpc_header_read
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_header_read)
#endif
static wl_rpc_err_t calculator_rpc_header_read(const uint8_t *data, size_t length,
    uint8_t kind, uint32_t *operation_id, int32_t *status, uint64_t *session,
    size_t *header_length) {
  uint32_t operation;
  size_t at = 9U;
  uint8_t byte;
  bool rejection;
  if (data == NULL || length < 9U) return WL_RPC_ERR_MALFORMED_METADATA;
  *session = ((uint64_t)calculator_rpc_read_u32(data) << 32U) |
      calculator_rpc_read_u32(data + 4U);
  byte = data[8U];
  rejection = (byte & 1U) != 0U;
  operation = (uint32_t)((byte >> 1U) & 63U);
  for (unsigned shift = 6U; (byte & 128U) != 0U; shift += 7U) {
    if (at == length || shift > 27U) return WL_RPC_ERR_MALFORMED_METADATA;
    byte = data[at++];
    if ((shift == 27U && byte > 31U) || byte == 0U)
      return WL_RPC_ERR_MALFORMED_METADATA;
    operation |= (uint32_t)(byte & 127U) << shift;
  }
  *operation_id = operation;
  *status = 0;
  if (*session == 0U || operation == 0U || (rejection && kind == 1U))
    return WL_RPC_ERR_MALFORMED_METADATA;
  if (rejection) {
    uint32_t bits;
    if (length != at + 4U) return WL_RPC_ERR_MALFORMED_METADATA;
    bits = calculator_rpc_read_u32(data + at);
    *status = bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
    if (*status == 0) return WL_RPC_ERR_MALFORMED_METADATA;
    at += 4U;
  }
  *header_length = at;
  return WL_RPC_OK;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_header_write)
CALCULATOR_RPC_LAYOUT_calculator_rpc_header_write
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_header_write)
#endif
static size_t calculator_rpc_header_write(uint8_t *data, uint8_t kind,
    uint32_t operation_id, int32_t status, uint64_t session) {
  size_t at = 9U;
  (void)kind;
  calculator_rpc_write_u32(data, (uint32_t)(session >> 32U));
  calculator_rpc_write_u32(data + 4U, (uint32_t)session);
  data[8U] = (uint8_t)(((operation_id & 63U) << 1U) | (status != 0 ? 1U : 0U));
  operation_id >>= 6U;
  if (operation_id != 0U) data[8U] |= 128U;
  while (operation_id != 0U) {
    uint8_t byte = (uint8_t)(operation_id & 127U);
    operation_id >>= 7U;
    data[at++] = operation_id != 0U ? (uint8_t)(byte | 128U) : byte;
  }
  if (status != 0) { calculator_rpc_write_u32(data + at, (uint32_t)status); at += 4U; }
  return at;
}
/* Shared managed-RPC control flow. Typed adapters keep codec calls type-safe;
 * no function-pointer casts, heap, or extra persistent scratch are needed. */
typedef wl_codec_status_t (*calculator_rpc_response_encoder_fn)(const void *,
    uint8_t *, size_t, size_t *, bool);

#if CALCULATOR_RUNTIME_HAS_RPC_SERVER
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_request_prepare)
CALCULATOR_RPC_LAYOUT_calculator_rpc_request_prepare
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_request_prepare)
#endif
static bool calculator_rpc_request_prepare(const wl_event_t *event,
    calculator_runtime_t *runtime, wl_event_type_t expected,
    calculator_runtime_result_t *result, uint64_t *session, size_t *header_length) {
  int32_t status = 0;
  result->detail_kind = CALCULATOR_RUNTIME_DETAIL_RPC;
  if (event->type != expected) {
    result->domain = CALCULATOR_RUNTIME_DELIVERY_MISMATCH;
    return false;
  }
  if (runtime->rpc_server == NULL) {
    result->domain = CALCULATOR_RUNTIME_MISSING_ROUTE;
    return false;
  }
  result->detail.rpc.rpc_result = calculator_rpc_header_read(event->payload,
      event->payload_len, 1U, &result->detail.rpc.operation_id, &status, session, header_length);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = CALCULATOR_RUNTIME_RPC_ERROR;
    return false;
  }
  if (event->type == WL_EVT_RELIABLE_RX && event->peer_session_id != *session) {
    result->detail.rpc.rpc_result = WL_RPC_ERR_SESSION_MISMATCH;
    result->domain = CALCULATOR_RUNTIME_RPC_ERROR;
    return false;
  }
  return true;
}

/* Return true only for a new request. Peer observation must remain after
 * successful decoding/fingerprinting: malformed input cannot cancel a peer. */
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_request_begin)
CALCULATOR_RPC_LAYOUT_calculator_rpc_request_begin
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_request_begin)
#endif
static bool calculator_rpc_request_begin(wl_ctx_t *ctx, calculator_runtime_t *runtime,
    const wl_rpc_request_identity_t *identity, size_t canonical_length,
    wl_time_ms_t now_ms, calculator_runtime_result_t *result) {
  wl_rpc_server_request_t request = {0};
  wl_rpc_server_response_t replay = {0};
  if (runtime->rpc_peer.session_id != identity->peer_session_id) {
    wl_rpc_peer_observation_t observation = {0};
    result->detail.rpc.rpc_result = calculator_runtime_peer_observe(ctx, runtime,
        identity->peer_session_id, &observation);
    if (result->detail.rpc.rpc_result != WL_RPC_OK) {
      result->domain = CALCULATOR_RUNTIME_RPC_ERROR;
      return false;
    }
    result->detail.rpc.peer_changed = observation.changed;
  }
  (void)canonical_length; /* Fingerprinting uses the unchanged public canonical codec. */
  result->detail.rpc.rpc_result = wl_rpc_server_begin(runtime->rpc_server,
      identity, now_ms, &result->detail.rpc.rpc_disposition, &request, &replay);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = CALCULATOR_RUNTIME_RPC_ERROR;
    return false;
  }
  switch (result->detail.rpc.rpc_disposition) {
    case WL_RPC_SERVER_NEW:
      result->detail.rpc.server_request = request;
      return true;
    case WL_RPC_SERVER_PENDING_DUPLICATE:
      result->domain = CALCULATOR_RUNTIME_OK;
      break;
    case WL_RPC_SERVER_REPLAY:
      result->detail.rpc.server_response = replay;
      result->detail.rpc.application_result = replay.application_status;
      result->detail.rpc.payload_length = replay.response_length;
      result->domain = CALCULATOR_RUNTIME_OK;
      break;
    case WL_RPC_SERVER_CONFLICT:
      result->detail.rpc.rpc_result = WL_RPC_ERR_OPERATION_CONFLICT;
      result->domain = CALCULATOR_RUNTIME_RPC_ERROR;
      break;
    default:
      result->detail.rpc.rpc_result = WL_RPC_ERR_INVALID_STATE;
      result->domain = CALCULATOR_RUNTIME_RPC_ERROR;
      break;
  }
  return false;
}

#endif

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_finish_response)
CALCULATOR_RPC_LAYOUT_calculator_rpc_finish_response
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_finish_response)
#endif
static void calculator_rpc_finish_response(calculator_runtime_t *runtime,
    const wl_rpc_server_request_t *request, int32_t application_status,
    const void *response, wl_time_ms_t now_ms, bool owned,
    calculator_rpc_response_encoder_fn encode, calculator_runtime_result_t *result) {
  wl_rpc_server_response_buffer_t buffer = {0};
  wl_rpc_server_response_t cached = {0};
  size_t encoded_length = 0U;
  const size_t header_length = calculator_rpc_header_size(request->identity.operation_id, application_status);
  result->detail.rpc.operation_id = request->identity.operation_id;
  result->detail.rpc.rpc_result = wl_rpc_server_response_prepare(runtime->rpc_server, request, &buffer);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = CALCULATOR_RUNTIME_RPC_ERROR;
    return;
  }
  if (buffer.capacity < header_length) {
    result->detail.rpc.rpc_result = WL_RPC_ERR_RESPONSE_TOO_LARGE;
    result->domain = CALCULATOR_RUNTIME_RPC_ERROR;
    return;
  }
  calculator_rpc_header_write(buffer.data, 2U, request->identity.operation_id,
      application_status, request->identity.peer_session_id);
  if (application_status == 0) {
    result->detail.rpc.codec_status = encode(response, buffer.data + header_length,
        buffer.capacity - header_length, &encoded_length, owned);
    if (result->detail.rpc.codec_status != WL_CODEC_OK) {
      result->domain = CALCULATOR_RUNTIME_CODEC_ERROR;
      return;
    }
  }
  result->detail.rpc.rpc_result = wl_rpc_server_response_commit(runtime->rpc_server,
      &buffer, application_status, encoded_length + header_length, now_ms, &cached);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = CALCULATOR_RUNTIME_RPC_ERROR;
    return;
  }
  result->detail.rpc.server_response = cached;
  result->detail.rpc.payload_length = cached.response_length;
  result->domain = CALCULATOR_RUNTIME_OK;
}
static const uint64_t calculator_rpc_fingerprint_seed = UINT64_C(0x24faaea3493c1c2e);
wl_codec_status_t add_request_wlc_detail_fingerprint(const add_request_t *, uint64_t *, size_t *);
#if ADD_REQUEST_HAS_VALUE
void add_request_wlc_detail_value_copy(const add_request_t *, add_request_value_t *);
#endif

typedef struct { wl_ctx_t *link; calculator_runtime_t *runtime; } calculator_peer_cancel_context_t;
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_cancel_peer_tx)
CALCULATOR_RPC_LAYOUT_calculator_runtime_cancel_peer_tx
#endif
static void calculator_runtime_cancel_peer_tx(void *context, wl_tx_handle_t handle) {
  calculator_peer_cancel_context_t *cancel = context;
  wl_tx_result_t ignored;
  if (cancel == NULL) return;
  (void)wl_tx_cancel(cancel->link, handle);
  /* A cancelled transaction need not emit a terminal event. Take it now, or
   * retain just its handle until the adapter releases physical TX storage. */
  if (wl_tx_take(cancel->link, handle, &ignored) == WL_ERR_INVALID_STATE)
    cancel->runtime->rpc_retiring_tx = handle;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_config_defaults)
CALCULATOR_RPC_LAYOUT_calculator_runtime_config_defaults
#endif
wl_err_t calculator_runtime_config_defaults(calculator_runtime_config_t *config) {
  if (config == NULL) return WL_ERR_INVALID_ARG;
  memset(config, 0, sizeof(*config));
  config->rpc_client_slot_count = 1U;
  config->rpc_client_next_operation_id = 1U;
  config->rpc_server_pending_slot_count = 1U;
  config->rpc_server_cache_slot_count = 1U;
  config->rpc_server_response_capacity = 19U;
  config->rpc_server_cache_policy = WL_RPC_CACHE_REJECT_NEW;
  return WL_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_config_enable_client)
CALCULATOR_RPC_LAYOUT_calculator_runtime_config_enable_client
#endif
wl_err_t calculator_runtime_config_enable_client(calculator_runtime_config_t *config) {
  if (config == NULL) return WL_ERR_INVALID_ARG;
  if (!CALCULATOR_RUNTIME_HAS_RPC_CLIENT || config->rpc_client_slot_count == 0U) return WL_ERR_NOT_SUPPORTED;
  config->rpc_client_enabled = 1U;
  return WL_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_config_enable_server)
CALCULATOR_RPC_LAYOUT_calculator_runtime_config_enable_server
#endif
wl_err_t calculator_runtime_config_enable_server(calculator_runtime_config_t *config) {
  if (config == NULL) return WL_ERR_INVALID_ARG;
  if (!CALCULATOR_RUNTIME_HAS_RPC_SERVER || config->rpc_server_pending_slot_count == 0U || config->rpc_server_cache_slot_count == 0U || config->rpc_server_response_capacity == 0U) return WL_ERR_NOT_SUPPORTED;
  config->rpc_server_enabled = 1U;
  return WL_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_default_storage_descriptor)
CALCULATOR_RPC_LAYOUT_calculator_runtime_default_storage_descriptor
#endif
calculator_runtime_storage_t calculator_runtime_default_storage_descriptor(calculator_runtime_default_storage_t *storage) {
  calculator_runtime_storage_t descriptor = {0};
  if (storage != NULL) {
    descriptor.data = storage->bytes;
    descriptor.size = sizeof(storage->bytes);
    descriptor.rpc_results = storage->rpc_results;
    descriptor.rpc_result_count = 1U;
  }
  return descriptor;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_roles_valid)
CALCULATOR_RPC_LAYOUT_calculator_runtime_roles_valid
#endif
static int calculator_runtime_roles_valid(const calculator_runtime_config_t *config) {
  (void)config;
  return WL_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_init_issue_str)
CALCULATOR_RPC_LAYOUT_calculator_runtime_init_issue_str
#endif
const char *calculator_runtime_init_issue_str(calculator_runtime_init_issue_t issue) {
  switch (issue) {
    case CALCULATOR_RUNTIME_INIT_OK: return "ok";
    case CALCULATOR_RUNTIME_INIT_NULL_ARGUMENT: return "null argument";
    case CALCULATOR_RUNTIME_INIT_ROLE_ENABLE: return "role enable must be zero or one";
    case CALCULATOR_RUNTIME_INIT_RETAINED_CAPACITY: return "retained capacity is zero";
    case CALCULATOR_RUNTIME_INIT_RPC_CLIENT_CAPACITY: return "RPC client capacity is zero";
    case CALCULATOR_RUNTIME_INIT_RPC_SERVER_CAPACITY: return "RPC server capacity is zero";
    case CALCULATOR_RUNTIME_INIT_RPC_TIMEOUT: return "RPC timeout exceeds wrap-safe range";
    case CALCULATOR_RUNTIME_INIT_RPC_CACHE_POLICY: return "unknown RPC cache policy";
    case CALCULATOR_RUNTIME_INIT_LAYOUT_OVERFLOW: return "runtime layout size overflow";
    case CALCULATOR_RUNTIME_INIT_STORAGE_TOO_SMALL: return "runtime storage is too small";
    case CALCULATOR_RUNTIME_INIT_STORAGE_NULL: return "runtime storage data is null";
    case CALCULATOR_RUNTIME_INIT_STORAGE_ALIGNMENT: return "runtime storage is misaligned";
    case CALCULATOR_RUNTIME_INIT_STORAGE_OVERLAP: return "runtime storage overlaps the instance";
    case CALCULATOR_RUNTIME_INIT_COMPONENT: return "runtime component initialization failed";
    default: return "unknown runtime initialization issue";
  }
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_init_failure)
CALCULATOR_RPC_LAYOUT_calculator_runtime_init_failure
#endif
static int calculator_runtime_init_failure(calculator_runtime_init_diagnostic_t *diagnostic, calculator_runtime_init_issue_t issue, const char *field, size_t required, size_t provided, int result) {
  if (diagnostic != NULL) {
    diagnostic->issue = issue;
    diagnostic->field = field;
    diagnostic->required = required;
    diagnostic->provided = provided;
  }
  return result;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_result_storage_validate)
CALCULATOR_RPC_LAYOUT_calculator_runtime_result_storage_validate
#endif
static int calculator_runtime_result_storage_validate(const calculator_runtime_instance_t *instance, const calculator_runtime_config_t *config, const calculator_runtime_storage_t *storage, calculator_runtime_init_diagnostic_t *diagnostic) {
  uintptr_t address, other;
  size_t bytes;
  if (!config->rpc_client_enabled) return WL_OK;
  if (storage->rpc_result_count < config->rpc_client_slot_count) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_STORAGE_TOO_SMALL, "storage.rpc_result_count", config->rpc_client_slot_count, storage->rpc_result_count, WL_ERR_BUF_TOO_SMALL);
  if (storage->rpc_results == NULL) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_STORAGE_NULL, "storage.rpc_results", 1U, 0U, WL_ERR_INVALID_ARG);
  if (storage->rpc_result_count > SIZE_MAX / sizeof(*storage->rpc_results)) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_LAYOUT_OVERFLOW, "storage.rpc_result_count", 0U, storage->rpc_result_count, WL_ERR_INVALID_ARG);
  address = (uintptr_t)(void *)storage->rpc_results;
  if (address % _Alignof(calculator_rpc_result_value_t) != 0U) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_STORAGE_ALIGNMENT, "storage.rpc_results", _Alignof(calculator_rpc_result_value_t), address % _Alignof(calculator_rpc_result_value_t), WL_ERR_INVALID_ARG);
  bytes = storage->rpc_result_count * sizeof(*storage->rpc_results);
  other = (uintptr_t)(const void *)instance;
  if ((address <= other && other - address < bytes) || (other < address && address - other < sizeof(*instance))) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_STORAGE_OVERLAP, "storage.rpc_results", bytes, 0U, WL_ERR_INVALID_ARG);
  other = (uintptr_t)storage->data;
  if (storage->size != 0U && ((address <= other && other - address < bytes) || (other < address && address - other < storage->size))) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_STORAGE_OVERLAP, "storage.rpc_results", bytes, 0U, WL_ERR_INVALID_ARG);
  return WL_OK;
}

typedef struct {
  uint8_t *base;
  size_t size;
  size_t offset;
} calculator_runtime_storage_cursor_t;

typedef struct {
  void *rpc_client_slots;
  void *rpc_server_pending_slots;
  void *rpc_server_cache_slots;
  void *rpc_server_responses;
  size_t rpc_server_responses_size;
} calculator_runtime_layout_t;

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_storage_region)
CALCULATOR_RPC_LAYOUT_calculator_runtime_storage_region
#endif
static inline int calculator_runtime_storage_region(calculator_runtime_storage_cursor_t *cursor, size_t alignment, size_t count, size_t element_size, void **out_data, size_t *out_size) {
  size_t aligned;
  size_t region_size;
  if (cursor == NULL || alignment == 0U || (alignment & (alignment - 1U)) != 0U) return WL_ERR_INVALID_ARG;
  if (out_data != NULL) *out_data = NULL;
  if (out_size != NULL) *out_size = 0U;
  if (count != 0U && element_size > SIZE_MAX / count) return WL_ERR_INVALID_ARG;
  region_size = count * element_size;
  if (cursor->offset > SIZE_MAX - (alignment - 1U)) return WL_ERR_INVALID_ARG;
  aligned = (cursor->offset + (alignment - 1U)) & ~(alignment - 1U);
  if (region_size > SIZE_MAX - aligned) return WL_ERR_INVALID_ARG;
  if (aligned + region_size > cursor->size) return WL_ERR_BUF_TOO_SMALL;
  if (out_data != NULL && cursor->base != NULL) *out_data = cursor->base + aligned;
  if (out_size != NULL) *out_size = region_size;
  cursor->offset = aligned + region_size;
  return WL_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_layout)
CALCULATOR_RPC_LAYOUT_calculator_runtime_layout
#endif
static int calculator_runtime_layout(const calculator_runtime_config_t *config, uint8_t *base, size_t size, calculator_runtime_layout_t *out_layout, calculator_runtime_requirements_t *out_requirements) {
  calculator_runtime_storage_cursor_t cursor = {base, size, 0U};
  size_t alignment = 1U;
  int result;
  if (out_layout != NULL) memset(out_layout, 0, sizeof(*out_layout));
  if (out_requirements != NULL) memset(out_requirements, 0, sizeof(*out_requirements));
  if (config == NULL) return WL_ERR_INVALID_ARG;
  if (calculator_runtime_roles_valid(config) != WL_OK) return WL_ERR_NOT_SUPPORTED;
  if (config->rpc_client_enabled > 1U || config->rpc_server_enabled > 1U) return WL_ERR_INVALID_ARG;
  if (config->rpc_client_enabled != 0U) {
    if (config->rpc_client_slot_count == 0U) return WL_ERR_INVALID_ARG;
    if (alignment < _Alignof(wl_rpc_client_slot_t)) alignment = _Alignof(wl_rpc_client_slot_t);
    result = calculator_runtime_storage_region(&cursor, _Alignof(wl_rpc_client_slot_t), config->rpc_client_slot_count, sizeof(wl_rpc_client_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_client_slots, NULL);
    if (result != WL_OK) return result;
  }
  if (config->rpc_server_enabled != 0U) {
    if (config->rpc_server_pending_slot_count == 0U || config->rpc_server_cache_slot_count == 0U || config->rpc_server_response_capacity == 0U) return WL_ERR_INVALID_ARG;
    if (config->rpc_server_pending_timeout_ms >= UINT32_C(0x80000000)) return WL_ERR_INVALID_ARG;
    if (config->rpc_server_cache_ttl_ms >= UINT32_C(0x80000000)) return WL_ERR_INVALID_ARG;
    if (config->rpc_server_cache_policy != WL_RPC_CACHE_REJECT_NEW && config->rpc_server_cache_policy != WL_RPC_CACHE_EVICT_OLDEST) return WL_ERR_INVALID_ARG;
    if (alignment < _Alignof(wl_rpc_server_pending_slot_t)) alignment = _Alignof(wl_rpc_server_pending_slot_t);
    result = calculator_runtime_storage_region(&cursor, _Alignof(wl_rpc_server_pending_slot_t), config->rpc_server_pending_slot_count, sizeof(wl_rpc_server_pending_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_server_pending_slots, NULL);
    if (result != WL_OK) return result;
    if (alignment < _Alignof(wl_rpc_server_cache_slot_t)) alignment = _Alignof(wl_rpc_server_cache_slot_t);
    result = calculator_runtime_storage_region(&cursor, _Alignof(wl_rpc_server_cache_slot_t), config->rpc_server_cache_slot_count, sizeof(wl_rpc_server_cache_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_server_cache_slots, NULL);
    if (result != WL_OK) return result;
    result = calculator_runtime_storage_region(&cursor, 1U, config->rpc_server_cache_slot_count, config->rpc_server_response_capacity, out_layout == NULL ? NULL : &out_layout->rpc_server_responses, out_layout == NULL ? NULL : &out_layout->rpc_server_responses_size);
    if (result != WL_OK) return result;
  }
  if (out_requirements != NULL) out_requirements->rpc_result_count = config->rpc_client_enabled ? config->rpc_client_slot_count : 0U;
  if (out_requirements != NULL) {
    out_requirements->storage_size = cursor.offset;
    out_requirements->storage_alignment = alignment;
  }
  return WL_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_requirements)
CALCULATOR_RPC_LAYOUT_calculator_runtime_requirements
#endif
int calculator_runtime_requirements(const calculator_runtime_config_t *config, calculator_runtime_requirements_t *out_requirements) {
  calculator_runtime_config_t config_copy;
  if (config == NULL || out_requirements == NULL) return WL_ERR_INVALID_ARG;
  config_copy = *config;
  *out_requirements = (calculator_runtime_requirements_t){0};
  return calculator_runtime_layout(&config_copy, NULL, SIZE_MAX, NULL, out_requirements);
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_init_validate)
CALCULATOR_RPC_LAYOUT_calculator_runtime_init_validate
#endif
static int calculator_runtime_init_validate(const calculator_runtime_instance_t *instance, const calculator_runtime_config_t *config, const calculator_runtime_storage_t *storage, calculator_runtime_requirements_t *requirements, calculator_runtime_init_diagnostic_t *diagnostic) {
  uintptr_t instance_address;
  uintptr_t storage_address;
  int result;
  if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
  if (instance == NULL || config == NULL || storage == NULL || requirements == NULL || diagnostic == NULL)
    return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_NULL_ARGUMENT, instance == NULL ? "instance" : config == NULL ? "config" : storage == NULL ? "storage" : requirements == NULL ? "requirements" : "diagnostic", 1U, 0U, WL_ERR_INVALID_ARG);
  if (calculator_runtime_roles_valid(config) != WL_OK) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_ROLE_ENABLE, "endpoint.rpc_role", 0U, 1U, WL_ERR_NOT_SUPPORTED);
  if (config->rpc_client_enabled > 1U) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_ROLE_ENABLE, "rpc_client_enabled", 1U, config->rpc_client_enabled, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled > 1U) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_ROLE_ENABLE, "rpc_server_enabled", 1U, config->rpc_server_enabled, WL_ERR_INVALID_ARG);
  if (config->rpc_client_enabled != 0U && config->rpc_client_slot_count == 0U) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_RPC_CLIENT_CAPACITY, "rpc_client_slot_count", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_pending_slot_count == 0U) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_RPC_SERVER_CAPACITY, "rpc_server_pending_slot_count", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_cache_slot_count == 0U) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_RPC_SERVER_CAPACITY, "rpc_server_cache_slot_count", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_response_capacity == 0U) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_RPC_SERVER_CAPACITY, "rpc_server_response_capacity", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_pending_timeout_ms >= UINT32_C(0x80000000)) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_RPC_TIMEOUT, "rpc_server_pending_timeout_ms", UINT32_C(0x7fffffff), config->rpc_server_pending_timeout_ms, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_cache_ttl_ms >= UINT32_C(0x80000000)) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_RPC_TIMEOUT, "rpc_server_cache_ttl_ms", UINT32_C(0x7fffffff), config->rpc_server_cache_ttl_ms, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled && config->rpc_server_cache_policy != WL_RPC_CACHE_REJECT_NEW && config->rpc_server_cache_policy != WL_RPC_CACHE_EVICT_OLDEST) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_RPC_CACHE_POLICY, "rpc_server_cache_policy", 0U, (size_t)config->rpc_server_cache_policy, WL_ERR_INVALID_ARG);
  result = calculator_runtime_requirements(config, requirements);
  if (result != WL_OK) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_LAYOUT_OVERFLOW, "config", 0U, 0U, result);
  if (storage->size < requirements->storage_size) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_STORAGE_TOO_SMALL, "storage.size", requirements->storage_size, storage->size, WL_ERR_BUF_TOO_SMALL);
  if (requirements->storage_size != 0U) {
    if (storage->data == NULL) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_STORAGE_NULL, "storage.data", requirements->storage_size, 0U, WL_ERR_INVALID_ARG);
    if (((uintptr_t)storage->data & (requirements->storage_alignment - 1U)) != 0U) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_STORAGE_ALIGNMENT, "storage.data", requirements->storage_alignment, (size_t)((uintptr_t)storage->data & (requirements->storage_alignment - 1U)), WL_ERR_INVALID_ARG);
    instance_address = (uintptr_t)(const void *)instance;
    storage_address = (uintptr_t)storage->data;
    if ((storage_address <= instance_address && instance_address - storage_address < requirements->storage_size) || (instance_address < storage_address && storage_address - instance_address < sizeof(*instance))) return calculator_runtime_init_failure(diagnostic, CALCULATOR_RUNTIME_INIT_STORAGE_OVERLAP, "storage.data", requirements->storage_size, storage->size, WL_ERR_INVALID_ARG);
  }
  result = calculator_runtime_result_storage_validate(instance, config, storage, diagnostic);
  if (result != WL_OK) return result;
  return WL_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_init)
CALCULATOR_RPC_LAYOUT_calculator_runtime_init
#endif
int calculator_runtime_init(calculator_runtime_instance_t *instance, const calculator_runtime_config_t *config, const calculator_runtime_storage_t *storage) {
  calculator_runtime_config_t config_copy;
  calculator_runtime_storage_t storage_copy;
  calculator_runtime_requirements_t requirements;
  calculator_runtime_layout_t layout;
  uintptr_t instance_address;
  uintptr_t storage_address;
  int result;
  if (instance == NULL || config == NULL || storage == NULL) return WL_ERR_INVALID_ARG;
  config_copy = *config;
  storage_copy = *storage;
  config = &config_copy;
  storage = &storage_copy;
  result = calculator_runtime_requirements(config, &requirements);
  if (result != WL_OK) return result;
  if (storage->size < requirements.storage_size) return WL_ERR_BUF_TOO_SMALL;
  if (requirements.storage_size != 0U) {
    if (storage->data == NULL || ((uintptr_t)storage->data & (requirements.storage_alignment - 1U)) != 0U) return WL_ERR_INVALID_ARG;
    instance_address = (uintptr_t)(void *)instance;
    storage_address = (uintptr_t)storage->data;
    if ((storage_address <= instance_address && instance_address - storage_address < requirements.storage_size) || (instance_address < storage_address && storage_address - instance_address < sizeof(*instance))) return WL_ERR_INVALID_ARG;
  }
  result = calculator_runtime_result_storage_validate(instance, config, storage, NULL);
  if (result != WL_OK) return result;
  result = calculator_runtime_layout(config, (uint8_t *)storage->data, storage->size, &layout, NULL);
  if (result != WL_OK) return result;
  memset(instance, 0, sizeof(*instance));
#if CALCULATOR_RUNTIME_HAS_RPC_CLIENT
  if (config->rpc_client_enabled != 0U) {
    const wl_rpc_client_config_t client_config = {
      (wl_rpc_client_slot_t *)layout.rpc_client_slots,
      config->rpc_client_slot_count,
      config->rpc_client_next_operation_id
    };
    if (wl_rpc_client_init(&instance->rpc_client, &client_config) != WL_RPC_OK) {
      result = WL_ERR_INVALID_ARG;
      goto init_failed;
    }
    instance->runtime.rpc_client = &instance->rpc_client;
    instance->runtime.rpc_results = storage->rpc_results;
    instance->runtime.rpc_result_count = storage->rpc_result_count;
  }
#endif
#if CALCULATOR_RUNTIME_HAS_RPC_SERVER
  if (config->rpc_server_enabled) {
    const wl_rpc_server_config_t server_config = {
      .pending_slots = (wl_rpc_server_pending_slot_t *)layout.rpc_server_pending_slots,
      .pending_slot_count = config->rpc_server_pending_slot_count,
      .pending_timeout_ms = config->rpc_server_pending_timeout_ms,
      .cache_slots = (wl_rpc_server_cache_slot_t *)layout.rpc_server_cache_slots,
      .cache_slot_count = config->rpc_server_cache_slot_count,
      .response_storage = (uint8_t *)layout.rpc_server_responses,
      .response_storage_size = layout.rpc_server_responses_size,
      .response_capacity_per_slot = config->rpc_server_response_capacity,
      .cache_ttl_ms = config->rpc_server_cache_ttl_ms,
      .cache_policy = config->rpc_server_cache_policy,
    };
    if (wl_rpc_server_init(&instance->rpc_server, &server_config) != WL_RPC_OK) {
      result = WL_ERR_INVALID_ARG;
      goto init_failed;
    }
    instance->runtime.rpc_server = &instance->rpc_server;
  }
#endif
#if CALCULATOR_RUNTIME_HAS_RPC_SERVER
  if (config->rpc_server_enabled != 0U) {
    instance->runtime.add.request_scratch = &instance->add_scratch.request;
    instance->runtime.add.request_handler = config->add_request_handler;
    instance->runtime.add.user_data = config->add_user_data;
  }
#endif
  return WL_OK;

init_failed:
  memset(instance, 0, sizeof(*instance));
  return result;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_init_checked)
CALCULATOR_RPC_LAYOUT_calculator_runtime_init_checked
#endif
int calculator_runtime_init_checked(calculator_runtime_instance_t *instance, const calculator_runtime_config_t *config, const calculator_runtime_storage_t *storage, calculator_runtime_init_diagnostic_t *out_diagnostic) {
  calculator_runtime_requirements_t requirements;
  int result = calculator_runtime_init_validate(instance, config, storage, &requirements, out_diagnostic);
  if (result != WL_OK) return result;
  result = calculator_runtime_init(instance, config, storage);
  if (result != WL_OK) return calculator_runtime_init_failure(out_diagnostic, CALCULATOR_RUNTIME_INIT_COMPONENT, "component", 0U, 0U, result);
  return WL_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_rpc_response_writable)
CALCULATOR_RPC_LAYOUT_calculator_rpc_response_writable
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_rpc_response_writable)
#endif
static uint8_t calculator_rpc_response_writable(const wl_ctx_t *ctx, const calculator_runtime_t *runtime) {
  uint16_t message_id;
  if (runtime->rpc_response_retry_blocked &&
      runtime->rpc_response_retry_generation == wl_link_resource_generation(ctx)) return 0U;
  if (runtime->rpc_server == NULL || wl_rpc_server_response_peek(runtime->rpc_server, &message_id) != WL_RPC_OK) return 0U;
  switch (message_id) {
    case ADD_RESPONSE_MESSAGE_ID: return wl_tx_can_submit(ctx, WL_DELIVERY_RELIABLE);
    default: return wl_tx_can_submit(ctx, WL_DELIVERY_UNRELIABLE);
  }
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_peer_observe)
CALCULATOR_RPC_LAYOUT_calculator_runtime_peer_observe
#endif
wl_rpc_err_t calculator_runtime_peer_observe(wl_ctx_t *ctx, calculator_runtime_t *runtime, uint64_t peer_session_id, wl_rpc_peer_observation_t *out_observation) {
  calculator_peer_cancel_context_t cancel = {ctx, runtime};
  wl_rpc_err_t result;
  if (out_observation != NULL) memset(out_observation, 0, sizeof(*out_observation));
  if (ctx == NULL || runtime == NULL || runtime->rpc_server == NULL || peer_session_id == 0U || out_observation == NULL) return WL_RPC_ERR_INVALID_ARG;
  result = wl_rpc_peer_observe(runtime->rpc_server, &runtime->rpc_peer, peer_session_id, calculator_runtime_cancel_peer_tx, &cancel, out_observation);
  if (result == WL_RPC_OK && out_observation->changed != 0U) runtime->rpc_peer_observation = *out_observation;
  return result;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_peer_observation_take)
CALCULATOR_RPC_LAYOUT_calculator_runtime_peer_observation_take
#endif
wl_rpc_err_t calculator_runtime_peer_observation_take(calculator_runtime_t *runtime, wl_rpc_peer_observation_t *out_observation) {
  if (out_observation != NULL) memset(out_observation, 0, sizeof(*out_observation));
  if (runtime == NULL || out_observation == NULL) return WL_RPC_ERR_INVALID_ARG;
  if (runtime->rpc_peer_observation.changed == 0U) return WL_RPC_ERR_NOT_FOUND;
  *out_observation = runtime->rpc_peer_observation;
  memset(&runtime->rpc_peer_observation, 0, sizeof(runtime->rpc_peer_observation));
  return WL_RPC_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_poll)
CALCULATOR_RPC_LAYOUT_calculator_runtime_poll
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_runtime_poll)
#endif
wl_rpc_err_t calculator_runtime_poll(calculator_runtime_t *runtime, wl_time_ms_t now_ms, calculator_runtime_poll_result_t *out_result) {
  wl_rpc_err_t result;
  wl_rpc_server_expiry_t server_expiry = {0};
  if (out_result != NULL) memset(out_result, 0, sizeof(*out_result));
  if (runtime == NULL || out_result == NULL) return WL_RPC_ERR_INVALID_ARG;
  if (runtime->rpc_client != NULL) {
    result = wl_rpc_client_poll(runtime->rpc_client, now_ms, &out_result->client_timed_out);
    if (result != WL_RPC_OK) return result;
  }
  if (runtime->rpc_server != NULL) {
    result = wl_rpc_server_expired_acquire(runtime->rpc_server, now_ms, &out_result->server_expired_request);
    if (result == WL_RPC_OK) out_result->server_pending_expired = 1U;
    else if (result != WL_RPC_ERR_NOT_FOUND) return result;
    result = wl_rpc_server_poll(runtime->rpc_server, now_ms, &server_expiry);
    if (result != WL_RPC_OK) return result;
    out_result->server_cache_expired = server_expiry.cache_expired;
  }
  return WL_RPC_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_service)
CALCULATOR_RPC_LAYOUT_calculator_runtime_service
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_runtime_service)
#endif
wl_rpc_err_t calculator_runtime_service(wl_ctx_t *ctx, calculator_runtime_t *runtime, wl_time_ms_t now_ms, calculator_runtime_service_result_t *out_result) {
  wl_rpc_server_response_t response = {0};
  wl_rpc_err_t result;
  uint8_t reliable_response = 0U;
  if (out_result != NULL) memset(out_result, 0, sizeof(*out_result));
  if (ctx == NULL || runtime == NULL || out_result == NULL) return WL_RPC_ERR_INVALID_ARG;
  out_result->response = calculator_runtime_result(NULL);
  if (runtime->rpc_retiring_tx != 0U) {
    wl_tx_result_t ignored;
    int retired = wl_tx_can_take(ctx, runtime->rpc_retiring_tx) ? wl_tx_take(ctx, runtime->rpc_retiring_tx, &ignored) : WL_ERR_BUSY;
    if (retired == WL_OK || retired == WL_ERR_NOT_FOUND) runtime->rpc_retiring_tx = 0U;
  }
  wl_rpc_deadline_hint_t timer;
  result = calculator_runtime_get_deadline_hint(runtime, now_ms, &timer);
  if (result != WL_RPC_OK) return result;
  if (timer.next_deadline_ms == 0U) {
    result = calculator_runtime_poll(runtime, now_ms, &out_result->deadlines);
    if (result != WL_RPC_OK) return result;
  }
  if (!calculator_rpc_response_writable(ctx, runtime)) return WL_RPC_OK;
  runtime->rpc_response_retry_blocked = 0U;
  result = wl_rpc_server_response_acquire(runtime->rpc_server, &response);
  if (result == WL_RPC_ERR_NOT_FOUND) return WL_RPC_OK;
  if (result != WL_RPC_OK) return result;
  out_result->response.message_id = response.identity.response_message_id;
  out_result->response.detail_kind = CALCULATOR_RUNTIME_DETAIL_RPC;
  out_result->response.detail.rpc.operation_id = response.identity.operation_id;
  out_result->response.detail.rpc.application_result = response.application_status;
  out_result->response.detail.rpc.payload_length = response.response_length;
  out_result->response.detail.rpc.server_response = response;
  switch (response.identity.response_message_id) {
    case ADD_RESPONSE_MESSAGE_ID:
      if (response.identity.request_message_id != ADD_REQUEST_MESSAGE_ID) {
        result = WL_RPC_ERR_RESPONSE_MISMATCH;
        break;
      }
      reliable_response = 1U;
      break;
    default:
      result = WL_RPC_ERR_RESPONSE_MISMATCH;
      break;
  }
  if (result != WL_RPC_OK) {
    (void)wl_rpc_server_response_defer(runtime->rpc_server, &response);
    return result;
  }
  if (reliable_response != 0U) {
    out_result->response.detail.rpc.core_result = wl_send_reliable(ctx, response.identity.response_message_id, response.response_data, response.response_length, now_ms, &out_result->response.detail.rpc.handle);
  } else {
    out_result->response.detail.rpc.core_result = wl_send_unreliable(ctx, response.identity.response_message_id, response.response_data, response.response_length);
  }
  if (out_result->response.detail.rpc.core_result != WL_OK) {
    result = wl_rpc_server_response_defer(runtime->rpc_server, &response);
    if (result != WL_RPC_OK) return result;
    out_result->response.domain = CALCULATOR_RUNTIME_CORE_ERROR;
    out_result->responses_deferred = 1U;
    runtime->rpc_response_retry_generation = wl_link_resource_generation(ctx);
    runtime->rpc_response_retry_blocked = 1U;
    return WL_RPC_OK;
  }
  if (reliable_response != 0U) {
    result = wl_rpc_server_response_submitted(runtime->rpc_server, &response, out_result->response.detail.rpc.handle);
  } else {
    result = wl_rpc_server_response_sent(runtime->rpc_server, &response);
  }
  if (result != WL_RPC_OK) {
    (void)wl_rpc_server_response_defer(runtime->rpc_server, &response);
    return result;
  }
  out_result->response.domain = CALCULATOR_RUNTIME_OK;
  out_result->responses_submitted = 1U;
  return WL_RPC_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_get_deadline_hint)
CALCULATOR_RPC_LAYOUT_calculator_runtime_get_deadline_hint
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_runtime_get_deadline_hint)
#endif
wl_rpc_err_t calculator_runtime_get_deadline_hint(const calculator_runtime_t *runtime, wl_time_ms_t now_ms, wl_rpc_deadline_hint_t *out_hint) {
  wl_rpc_deadline_hint_t component = {WL_RPC_NO_DEADLINE_MS};
  wl_rpc_err_t result;
  uint32_t nearest = WL_RPC_NO_DEADLINE_MS;
  if (out_hint != NULL) out_hint->next_deadline_ms = WL_RPC_NO_DEADLINE_MS;
  if (runtime == NULL || out_hint == NULL) return WL_RPC_ERR_INVALID_ARG;
  if (runtime->rpc_client != NULL) {
    result = wl_rpc_client_get_deadline_hint(runtime->rpc_client, now_ms, &component);
    if (result != WL_RPC_OK) return result;
    if (component.next_deadline_ms < nearest) nearest = component.next_deadline_ms;
  }
  if (runtime->rpc_server != NULL) {
    result = wl_rpc_server_get_deadline_hint(runtime->rpc_server, now_ms, &component);
    if (result != WL_RPC_OK) return result;
    if (component.next_deadline_ms < nearest) nearest = component.next_deadline_ms;
  }
  out_hint->next_deadline_ms = nearest;
  return WL_RPC_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_readiness)
CALCULATOR_RPC_LAYOUT_calculator_runtime_readiness
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_runtime_readiness)
#endif
wl_readiness_t calculator_runtime_readiness(const wl_ctx_t *ctx, const calculator_runtime_t *runtime, wl_time_ms_t now_ms) {
  wl_readiness_t value = wl_readiness_idle();
  wl_rpc_deadline_hint_t timer;
  uint16_t message_id = 0U;
  if (runtime == NULL || ctx == NULL) return value;
  if (calculator_runtime_get_deadline_hint(runtime, now_ms, &timer) == WL_RPC_OK)
    value = wl_readiness_at(timer.next_deadline_ms);
  if (runtime->rpc_retiring_tx != 0U) {
    if (wl_tx_can_take(ctx, runtime->rpc_retiring_tx)) value.runnable = 1U;
    else value.blocked = 1U;
  }
  if (runtime->rpc_server != NULL && wl_rpc_server_response_peek(runtime->rpc_server, &message_id) == WL_RPC_OK) {
    wl_delivery_t delivery = WL_DELIVERY_UNRELIABLE;
    switch (message_id) {
      case ADD_RESPONSE_MESSAGE_ID: delivery = WL_DELIVERY_RELIABLE; break;
      default: break;
    }
    if (wl_tx_can_submit(ctx, delivery) && (!runtime->rpc_response_retry_blocked ||
        runtime->rpc_response_retry_generation != wl_link_resource_generation(ctx))) value.runnable = 1U;
    else value.blocked = 1U;
  }
  return value;
}

/* Keep RPC routing separate from the pump under LTO. */
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_dispatch_event)
CALCULATOR_RPC_LAYOUT_calculator_runtime_dispatch_event
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_runtime_dispatch_event)
#endif
calculator_runtime_result_t calculator_runtime_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, calculator_runtime_t *runtime, wl_time_ms_t now_ms) {
  calculator_runtime_result_t result = calculator_runtime_result(event);
  if (event == NULL) return result;
  (void)now_ms;
  if (event->type == WL_EVT_TX_SUCCESS || event->type == WL_EVT_TX_TIMEOUT || event->type == WL_EVT_TX_FAILED) {
    wl_tx_result_t tx_result = {0};
    if (runtime == NULL || ctx == NULL) {
      result.domain = CALCULATOR_RUNTIME_NON_RX;
      return result;
    }
    result.detail_kind = CALCULATOR_RUNTIME_DETAIL_RPC;
    result.detail.rpc.handle = event->handle;
#if CALCULATOR_RUNTIME_HAS_MANAGED_RPC
    if (runtime->rpc_async != NULL && wl_rpc_async_retire_tx(runtime->rpc_async, event->handle)) {
      result.detail.rpc.core_result = wl_tx_take(ctx, event->handle, &tx_result);
      result.event_consumed = result.detail.rpc.core_result == WL_OK ? 1U : 0U;
      result.domain = result.detail.rpc.core_result == WL_OK ? CALCULATOR_RUNTIME_OK : CALCULATOR_RUNTIME_CORE_ERROR;
      return result;
    }
#endif
    if (runtime->rpc_server != NULL) {
      result.detail.rpc.rpc_result = wl_rpc_server_on_tx_event(runtime->rpc_server, event);
      if (result.detail.rpc.rpc_result == WL_RPC_OK) {
        result.detail.rpc.core_result = wl_tx_take(ctx, event->handle, &tx_result);
        result.event_consumed = result.detail.rpc.core_result == WL_OK ? 1U : 0U;
        result.domain = result.detail.rpc.core_result == WL_OK ? CALCULATOR_RUNTIME_OK : CALCULATOR_RUNTIME_CORE_ERROR;
        return result;
      }
      if (result.detail.rpc.rpc_result != WL_RPC_ERR_NOT_FOUND) {
        result.domain = CALCULATOR_RUNTIME_RPC_ERROR;
        return result;
      }
    }
    if (runtime->rpc_client != NULL) {
      result.detail.rpc.rpc_result = wl_rpc_client_on_tx_event(runtime->rpc_client, event);
      if (result.detail.rpc.rpc_result == WL_RPC_OK) {
        result.detail.rpc.core_result = wl_tx_take(ctx, event->handle, &tx_result);
        result.event_consumed = result.detail.rpc.core_result == WL_OK ? 1U : 0U;
        result.domain = result.detail.rpc.core_result == WL_OK ? CALCULATOR_RUNTIME_OK : CALCULATOR_RUNTIME_CORE_ERROR;
      } else if (result.detail.rpc.rpc_result == WL_RPC_ERR_NOT_FOUND) result.domain = CALCULATOR_RUNTIME_NON_RX;
      else result.domain = CALCULATOR_RUNTIME_RPC_ERROR;
    } else {
      result.domain = CALCULATOR_RUNTIME_NON_RX;
    }
    return result;
  }
  if (event->type != WL_EVT_UNRELIABLE_RX && event->type != WL_EVT_RELIABLE_RX) {
    result.domain = CALCULATOR_RUNTIME_NON_RX;
    return result;
  }
  if (ctx == NULL) return result;
  if (runtime == NULL) goto release_event;

  switch (event->message_id) {
    case 20U: {
      wl_rpc_request_identity_t identity = {.request_fingerprint = calculator_rpc_fingerprint_seed};
      wl_rpc_server_request_t server_request;
      calculator_add_request_token_t token;
      size_t canonical_length = 0U, header_length = 0U;
      uint64_t session = 0U;
      if (!calculator_rpc_request_prepare(event, runtime, WL_EVT_RELIABLE_RX, &result, &session, &header_length)) break;
      if (runtime->add.request_scratch == NULL) {
        result.domain = CALCULATOR_RUNTIME_MISSING_SCRATCH;
        break;
      }
      result.detail.rpc.codec_status = calculator_rpc_add_request_decode(event->payload + header_length,
          event->payload_len - header_length, runtime->add.request_scratch);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = CALCULATOR_RUNTIME_CODEC_ERROR;
        break;
      }
      result.detail.rpc.codec_status = calculator_rpc_add_request_wlc_detail_fingerprint(runtime->add.request_scratch,
          &identity.request_fingerprint, &canonical_length);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = CALCULATOR_RUNTIME_CODEC_ERROR;
        break;
      }
      result.detail.rpc.payload_length = event->payload_len;
      identity.operation_id = result.detail.rpc.operation_id;
      identity.request_message_id = 20U;
      identity.response_message_id = 21U;
      identity.peer_session_id = session;
      if (!calculator_rpc_request_begin(ctx, runtime, &identity, canonical_length, now_ms, &result)) break;
      server_request = result.detail.rpc.server_request;
      memset(&token, 0, sizeof(token));
      token.private_state.owner = runtime;
      token.private_state.incarnation = runtime->rpc_incarnation;
      token.private_state.request = server_request;
#if ADD_REQUEST_HAS_VALUE && ADD_RESPONSE_HAS_VALUE
      if (runtime->add.value_handler != NULL) {
        int32_t status;
        calculator_runtime_result_t completed;
        if (runtime->add.request_value == NULL || runtime->add.response_value == NULL) {
          (void)wl_rpc_server_abandon(runtime->rpc_server, &server_request);
          result.domain = CALCULATOR_RUNTIME_MISSING_SCRATCH;
          break;
        }
        /* The view was decoded and has not crossed an application callback. */
        add_request_wlc_detail_value_copy(
            runtime->add.request_scratch, runtime->add.request_value);
        add_response_value_clear(runtime->add.response_value);
        status = runtime->add.value_handler(runtime->add.value_user_data,
            runtime->add.request_value, runtime->add.response_value);
        completed = status == 0
            ? calculator_add_server_complete_value(runtime, &token, runtime->add.response_value, now_ms)
            : calculator_add_server_reject(runtime, &token, status, now_ms);
        result.domain = completed.domain;
        result.detail.rpc = completed.detail.rpc;
        if (!calculator_runtime_result_ok(&completed))
          (void)wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        break;
      }
#endif
      if (runtime->add.request_handler == NULL) {
        result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        result.domain = CALCULATOR_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.rpc.application_result = runtime->add.request_handler(
          runtime->add.user_data, runtime->add.request_scratch, &token, WL_DELIVERY_RELIABLE);
      if (result.detail.rpc.application_result != 0) {
        result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        result.domain = CALCULATOR_RUNTIME_APPLICATION_ERROR;
      } else result.domain = CALCULATOR_RUNTIME_OK;
      break;
    }
    case 21U: {
      wl_rpc_client_response_t authority;
      uint16_t result_index;
      uint64_t session = 0U;
      size_t header_length = 0U;
      result.detail_kind = CALCULATOR_RUNTIME_DETAIL_RPC;
      if (event->type != WL_EVT_RELIABLE_RX) {
        result.domain = CALCULATOR_RUNTIME_DELIVERY_MISMATCH;
        break;
      }
      if (runtime->rpc_client == NULL) {
        result.domain = CALCULATOR_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.rpc.rpc_result = calculator_rpc_header_read(event->payload,
          event->payload_len, 2U, &result.detail.rpc.operation_id,
          &result.detail.rpc.application_result, &session, &header_length);
      if (result.detail.rpc.rpc_result != WL_RPC_OK) {
        result.detail.rpc.rpc_result = WL_RPC_ERR_MALFORMED_METADATA;
        result.domain = CALCULATOR_RUNTIME_RPC_ERROR;
        break;
      }
      if (session != wl_link_session_id(ctx)) {
        /* Observable stale/misdirected reply; do not touch another call. */
        result.detail.rpc.rpc_result = WL_RPC_ERR_SESSION_MISMATCH;
        result.domain = CALCULATOR_RUNTIME_OK;
        break;
      }
      result.detail.rpc.rpc_result = wl_rpc_client_response_prepare(runtime->rpc_client,
          20U, 21U, result.detail.rpc.operation_id, now_ms, &authority, &result_index);
      if (result.detail.rpc.rpc_result == WL_RPC_ERR_NOT_FOUND) {
        /* Unknown/late responses are observable, not failures of another call. */
        result.domain = CALCULATOR_RUNTIME_OK;
        break;
      }
      if (result.detail.rpc.rpc_result != WL_RPC_OK) {
        result.domain = result.detail.rpc.rpc_result == WL_RPC_ERR_INVALID_STATE ?
            CALCULATOR_RUNTIME_OK : CALCULATOR_RUNTIME_RPC_ERROR;
        break;
      }
      if (runtime->rpc_results == NULL || result_index >= runtime->rpc_result_count) {
        result.domain = CALCULATOR_RUNTIME_MISSING_SCRATCH;
        break;
      }
      if (result.detail.rpc.application_result == 0) {
        result.detail.rpc.codec_status = calculator_rpc_add_response_wlc_detail_value_decode(event->payload + header_length,
            event->payload_len - header_length, &runtime->rpc_results[result_index].add);
        if (result.detail.rpc.codec_status != WL_CODEC_OK) {
          result.domain = CALCULATOR_RUNTIME_CODEC_ERROR;
          break;
        }
      }
      result.detail.rpc.payload_length = event->payload_len;
      result.detail.rpc.rpc_result = wl_rpc_client_response_commit(runtime->rpc_client,
          &authority, result.detail.rpc.application_result, now_ms);
      result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ?
          CALCULATOR_RUNTIME_OK : CALCULATOR_RUNTIME_RPC_ERROR;
      break;
    }
    default:
      result.domain = CALCULATOR_RUNTIME_UNKNOWN_MESSAGE;
      break;
  }

release_event:
  wl_event_release(ctx, event);
  result.event_consumed = 1U;
  return result;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_add_client_start)
CALCULATOR_RPC_LAYOUT_calculator_add_client_start
#endif
calculator_runtime_result_t calculator_add_client_start(wl_ctx_t *ctx,
    calculator_runtime_t *runtime, const add_request_t *request, uint32_t timeout_ms,
    wl_time_ms_t now_ms) {
  calculator_runtime_result_t result = calculator_runtime_result(NULL);
  wl_tx_payload_claim_t claim = {0};
  uint32_t operation_id = 0U;
  size_t encoded_length = 0U, header_length;
  result.message_id = 20U;
  result.detail_kind = CALCULATOR_RUNTIME_DETAIL_RPC;
  if (ctx == NULL || runtime == NULL || runtime->rpc_client == NULL || request == NULL)
    return result;
  result.detail.rpc.rpc_result = wl_rpc_client_begin(runtime->rpc_client,
      20U, 21U, timeout_ms, now_ms, &operation_id);
  if (result.detail.rpc.rpc_result != WL_RPC_OK) {
    result.domain = CALCULATOR_RUNTIME_RPC_ERROR;
    return result;
  }
  result.detail.rpc.operation_id = operation_id;
  result.detail.rpc.core_result = wl_tx_payload_claim(ctx, 20U, WL_DELIVERY_RELIABLE, &claim);
  if (result.detail.rpc.core_result != WL_OK) {
    result.domain = CALCULATOR_RUNTIME_CORE_ERROR;
    goto start_failed;
  }
  header_length = calculator_rpc_header_size(operation_id, 0);
  if (claim.span.length < header_length) {
    result.detail.rpc.core_result = WL_ERR_BUF_TOO_SMALL;
    result.domain = CALCULATOR_RUNTIME_CORE_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    goto start_failed;
  }
  /* Encode directly into the link claim: no whole-request copy or heap. */
  calculator_rpc_header_write(claim.span.data, 1U, operation_id, 0, wl_link_session_id(ctx));
  result.detail.rpc.codec_status = calculator_rpc_add_request_encode(request, claim.span.data + header_length,
      claim.span.length - header_length, &encoded_length);
  if (result.detail.rpc.codec_status != WL_CODEC_OK) {
    result.detail.rpc.core_result = WL_ERR_CORRUPT_PAYLOAD;
    result.domain = CALCULATOR_RUNTIME_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    goto start_failed;
  }
  result.detail.rpc.payload_length = encoded_length + header_length;
  result.detail.rpc.core_result = wl_tx_payload_commit(ctx, &claim,
      encoded_length + header_length, now_ms, &result.detail.rpc.handle);
  if (result.detail.rpc.core_result != WL_OK) {
    result.domain = CALCULATOR_RUNTIME_CORE_ERROR;
    goto start_failed;
  }
  result.detail.rpc.rpc_result = wl_rpc_client_bind_tx(runtime->rpc_client, operation_id, result.detail.rpc.handle);
  result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ? CALCULATOR_RUNTIME_OK : CALCULATOR_RUNTIME_RPC_ERROR;
  return result;
start_failed:
  result.detail.rpc.rpc_result = wl_rpc_client_link_failed(runtime->rpc_client,
      operation_id, result.detail.rpc.core_result);
  if (result.detail.rpc.rpc_result == WL_RPC_OK)
    result.detail.rpc.rpc_result = wl_rpc_client_release(runtime->rpc_client, operation_id);
  if (result.detail.rpc.rpc_result == WL_RPC_OK) result.detail.rpc.operation_id = 0U;
  return result;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_add_client_inspect)
CALCULATOR_RPC_LAYOUT_calculator_add_client_inspect
#endif
wl_rpc_err_t calculator_add_client_inspect(const calculator_runtime_t *runtime,
    uint32_t operation_id, wl_rpc_client_result_t *out_client) {
  wl_rpc_err_t result;
  if (out_client != NULL) memset(out_client, 0, sizeof(*out_client));
  if (runtime == NULL || runtime->rpc_client == NULL || out_client == NULL)
    return WL_RPC_ERR_INVALID_ARG;
  result = wl_rpc_client_get(runtime->rpc_client, operation_id, out_client);
  if (result != WL_RPC_OK) return result;
  return out_client->request_message_id == 20U && out_client->response_message_id == 21U
      ? WL_RPC_OK : WL_RPC_ERR_RESPONSE_MISMATCH;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_add_client_release)
CALCULATOR_RPC_LAYOUT_calculator_add_client_release
#endif
wl_rpc_err_t calculator_add_client_release(calculator_runtime_t *runtime, uint32_t operation_id) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t result = calculator_add_client_inspect(runtime, operation_id, &client);
  return result == WL_RPC_OK ? wl_rpc_client_release(runtime->rpc_client, operation_id) : result;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_add_encode_response)
CALCULATOR_RPC_LAYOUT_calculator_add_encode_response
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_add_encode_response)
#endif
static wl_codec_status_t calculator_add_encode_response(const void *response,
    uint8_t *out, size_t capacity, size_t *length, bool owned) {
#if ADD_REQUEST_HAS_VALUE && ADD_RESPONSE_HAS_VALUE
  if (owned) return calculator_rpc_add_response_value_encode(response, out, capacity, length);
#else
  (void)owned;
#endif
  return calculator_rpc_add_response_encode(response, out, capacity, length);
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_add_server_finish)
CALCULATOR_RPC_LAYOUT_calculator_add_server_finish
#elif WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
CALCULATOR_RPC_LAYOUT(calculator_add_server_finish)
#endif
static calculator_runtime_result_t calculator_add_server_finish(calculator_runtime_t *runtime,
    const calculator_add_request_token_t *token, int32_t application_status,
    const void *response, wl_time_ms_t now_ms, bool owned) {
  calculator_runtime_result_t result = calculator_runtime_result(NULL);
  const wl_rpc_server_request_t *request;
  result.message_id = 21U;
  result.detail_kind = CALCULATOR_RUNTIME_DETAIL_RPC;
  result.detail.rpc.application_result = application_status;
  if (runtime == NULL || runtime->rpc_server == NULL || token == NULL ||
      token->private_state.owner != runtime ||
      token->private_state.incarnation != runtime->rpc_incarnation ||
      (application_status == 0 && response == NULL)) return result;
  request = &token->private_state.request;
  if (request->identity.request_message_id != 20U ||
      request->identity.response_message_id != 21U) return result;
  calculator_rpc_finish_response(runtime, request, application_status, response, now_ms,
      owned, calculator_add_encode_response, &result);
  return result;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_add_request_inspect)
CALCULATOR_RPC_LAYOUT_calculator_add_request_inspect
#endif
wl_rpc_err_t calculator_add_request_inspect(calculator_runtime_t *runtime,
    const calculator_add_request_token_t *token, wl_rpc_server_request_t *out_request) {
  if (runtime == NULL || token == NULL || out_request == NULL)
    return WL_RPC_ERR_INVALID_ARG;
  if (token->private_state.owner != runtime ||
      token->private_state.incarnation != runtime->rpc_incarnation ||
      token->private_state.request.identity.request_message_id != 20U ||
      token->private_state.request.identity.response_message_id != 21U)
    return WL_RPC_ERR_NOT_FOUND;
  *out_request = token->private_state.request;
  return WL_RPC_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_add_server_complete)
CALCULATOR_RPC_LAYOUT_calculator_add_server_complete
#endif
calculator_runtime_result_t calculator_add_server_complete(calculator_runtime_t *runtime,
    const calculator_add_request_token_t *token, const add_response_t *response, wl_time_ms_t now_ms) {
  return calculator_add_server_finish(runtime, token, 0, response, now_ms, false);
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_add_server_reject)
CALCULATOR_RPC_LAYOUT_calculator_add_server_reject
#endif
calculator_runtime_result_t calculator_add_server_reject(calculator_runtime_t *runtime,
    const calculator_add_request_token_t *token, int32_t application_status, wl_time_ms_t now_ms) {
  return calculator_add_server_finish(runtime, token, application_status, NULL, now_ms, false);
}

#if ADD_REQUEST_HAS_VALUE && ADD_RESPONSE_HAS_VALUE
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_add_server_complete_value)
CALCULATOR_RPC_LAYOUT_calculator_add_server_complete_value
#endif
calculator_runtime_result_t calculator_add_server_complete_value(calculator_runtime_t *runtime,
    const calculator_add_request_token_t *token, const add_response_value_t *response,
    wl_time_ms_t now_ms) {
  return calculator_add_server_finish(runtime, token, 0, response, now_ms, true);
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_add_encode_submission)
CALCULATOR_RPC_LAYOUT_calculator_add_encode_submission
#endif
wl_err_t calculator_add_encode_submission(const void *request, uint32_t operation_id, uint64_t session,
    uint8_t *out, size_t capacity, size_t *length) {
  wl_codec_status_t status;
  size_t size = 0U;
  const size_t header_length = calculator_rpc_header_size(operation_id, 0);
  if (capacity < header_length) return WL_ERR_BUF_TOO_SMALL;
  if (session == 0U) return WL_ERR_INVALID_ARG;
  calculator_rpc_header_write(out, 1U, operation_id, 0, session);
  status = calculator_rpc_add_request_value_encode(request, out + header_length, capacity - header_length, &size);
  if (status != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  *length = size + header_length;
  return WL_OK;
}
#endif

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_add_client_response)
CALCULATOR_RPC_LAYOUT_calculator_add_client_response
#endif
wl_rpc_err_t calculator_add_client_response(const calculator_runtime_t *runtime, uint32_t operation_id, const add_response_value_t **out_response) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t error;
  if (out_response == NULL) return WL_RPC_ERR_INVALID_ARG;
  *out_response = NULL;
  error = calculator_add_client_inspect(runtime, operation_id, &client);
  if (error != WL_RPC_OK) return error;
  if (!client.response_valid || runtime->rpc_results == NULL || client.result_index >= runtime->rpc_result_count) return WL_RPC_ERR_INVALID_STATE;
  *out_response = &runtime->rpc_results[client.result_index].add;
  return WL_RPC_OK;
}


#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_pump_event)
CALCULATOR_RPC_LAYOUT_calculator_runtime_pump_event
#endif
static wl_pump_event_disposition_t calculator_runtime_pump_event(void *user_data, wl_ctx_t *ctx, const wl_event_t *event, wl_time_ms_t now_ms) {
  calculator_runtime_pump_t *pump = (calculator_runtime_pump_t *)user_data;
  calculator_runtime_result_t result;
  if (pump == NULL || pump->runtime == NULL) return WL_PUMP_EVENT_UNHANDLED;
  result = calculator_runtime_dispatch_event(ctx, event, pump->runtime, now_ms);
  if (pump->on_result != NULL) pump->on_result(pump->user_data, &result);
  return result.event_consumed != 0U ? WL_PUMP_EVENT_CONSUMED : WL_PUMP_EVENT_UNHANDLED;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_pump_step)
CALCULATOR_RPC_LAYOUT_calculator_runtime_pump_step
#endif
static uint8_t calculator_runtime_pump_step(void *user_data, wl_ctx_t *ctx,
    wl_time_ms_t now_ms) {
  calculator_runtime_pump_t *pump = (calculator_runtime_pump_t *)user_data;
  uint8_t did_work = 0U;
  if (pump == NULL || pump->runtime == NULL) return 0U;
  /* The pump checked combined readiness before calling application_step.
   * Service rechecks its deadlines and transport before taking any work. */
  {
    pump->last_service_result = calculator_runtime_service(ctx, pump->runtime, now_ms, &pump->last_service);
    if (pump->last_service_result == WL_RPC_OK) {
      if (pump->last_service.response.message_id != 0U && pump->on_result != NULL)
        pump->on_result(pump->user_data, &pump->last_service.response);
      did_work = pump->last_service.responses_submitted != 0U ||
          pump->last_service.responses_deferred != 0U ||
          pump->last_service.deadlines.client_timed_out != 0U ||
          pump->last_service.deadlines.server_pending_expired != 0U ||
          pump->last_service.deadlines.server_cache_expired != 0U;
    }
  }
  if (pump->runtime->rpc_async != NULL &&
      wl_rpc_async_get_readiness(pump->runtime->rpc_async).runnable) {
    wl_rpc_async_service_result_t work = {0};
    if (wl_rpc_async_service(pump->runtime->rpc_async, now_ms, &work) != WL_OK)
      pump->last_service_result = WL_RPC_ERR_INVALID_STATE;
    did_work |= work.did_work;
  }
  return did_work;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_pump_readiness)
CALCULATOR_RPC_LAYOUT_calculator_runtime_pump_readiness
#endif
static wl_readiness_t calculator_runtime_pump_readiness(const void *user_data,
    const wl_ctx_t *ctx, wl_time_ms_t now_ms) {
  const calculator_runtime_pump_t *pump = (const calculator_runtime_pump_t *)user_data;
  if (pump == NULL || pump->runtime == NULL) return wl_readiness_idle();
  wl_readiness_t value = calculator_runtime_readiness(ctx, pump->runtime, now_ms);
  if (pump->runtime->rpc_async != NULL)
    wl_readiness_merge(&value, wl_rpc_async_get_readiness(pump->runtime->rpc_async));
  return value;
}
#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_pump_init)
CALCULATOR_RPC_LAYOUT_calculator_runtime_pump_init
#endif
wl_err_t calculator_runtime_pump_init(calculator_runtime_pump_t *pump, calculator_runtime_t *runtime, calculator_runtime_result_fn on_result, void *user_data) {
  if (pump == NULL || runtime == NULL) return WL_ERR_INVALID_ARG;
  memset(pump, 0, sizeof(*pump));
  pump->runtime = runtime;
  pump->user_data = user_data;
  pump->on_result = on_result;
  return WL_OK;
}

#if WLC_CALCULATOR_RPC_LAYOUT_ACTIVE && defined(CALCULATOR_RPC_LAYOUT_calculator_runtime_pump_hooks)
CALCULATOR_RPC_LAYOUT_calculator_runtime_pump_hooks
#endif
wl_pump_hooks_t calculator_runtime_pump_hooks(calculator_runtime_pump_t *pump) {
  wl_pump_hooks_t hooks = {0};
  if (pump == NULL) return hooks;
  hooks.application_user_data = pump;
  hooks.on_event = calculator_runtime_pump_event;
  hooks.application_step = calculator_runtime_pump_step;
  hooks.application_readiness = calculator_runtime_pump_readiness;
  return hooks;
}

#undef WLC_CALCULATOR_RPC_LAYOUT_ACTIVE
#ifdef WLC_CALCULATOR_RPC_LAYOUT_DEFAULT
#undef CALCULATOR_RPC_LAYOUT
#undef WLC_CALCULATOR_RPC_LAYOUT_DEFAULT
#endif
#ifdef WLC_CALCULATOR_RPC_LAYOUT_MODULE_DEFAULT
#undef CALCULATOR_RPC_LAYOUT_ENABLE
#undef WLC_CALCULATOR_RPC_LAYOUT_MODULE_DEFAULT
#endif
#ifdef WLC_CALCULATOR_RPC_LAYOUT_GLOBAL_DEFAULT
#undef WLC_RPC_LAYOUT_ENABLE
#undef WLC_CALCULATOR_RPC_LAYOUT_GLOBAL_DEFAULT
#endif
