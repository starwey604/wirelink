/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_CODEC_H
#define WIRELINK_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WL_HOT_ATTR
#define WL_HOT_ATTR
#endif

typedef struct {
  const uint8_t *data;
  size_t length;
} wl_codec_bytes_t;

typedef struct {
  const char *data;
  size_t length;
} wl_codec_string_t;

typedef int32_t wl_codec_status_t;
enum {
  WL_CODEC_OK = 0,
  WL_CODEC_ERR_MALFORMED,
  WL_CODEC_ERR_OVERFLOW,
  WL_CODEC_ERR_CAPACITY,
  WL_CODEC_ERR_WIRE_TYPE,
  WL_CODEC_ERR_DUPLICATE_FIELD,
  WL_CODEC_ERR_UTF8,
  WL_CODEC_ERR_INVALID_VALUE,
  WL_CODEC_ERR_MISSING_REQUIRED_FIELD
};

typedef enum wl_codec_kind {
  WL_CODEC_KIND_UINT32 = 0,
  WL_CODEC_KIND_INT32 = 1,
  WL_CODEC_KIND_SINT32 = 2,
  WL_CODEC_KIND_UINT64 = 3,
  WL_CODEC_KIND_INT64 = 4,
  WL_CODEC_KIND_SINT64 = 5,
  WL_CODEC_KIND_FIXED32 = 6,
  WL_CODEC_KIND_FIXED64 = 7,
  WL_CODEC_KIND_FLOAT32 = 8,
  WL_CODEC_KIND_FLOAT64 = 9,
  WL_CODEC_KIND_BOOL = 10,
  WL_CODEC_KIND_BYTES = 11,
  WL_CODEC_KIND_STRING = 12,
  WL_CODEC_KIND_INLINE_BYTES = 13,
  WL_CODEC_KIND_INLINE_STRING = 14,
  WL_CODEC_KIND_ENUM = 15,
  WL_CODEC_KIND_MESSAGE = 16
} wl_codec_kind_t;

typedef enum wl_codec_card {
  WL_CODEC_CARD_OPTIONAL = 0,
  WL_CODEC_CARD_REQUIRED = 1,
  WL_CODEC_CARD_REPEATED = 2,
  WL_CODEC_CARD_PACKED = 3,
  WL_CODEC_CARD_REQUIRED_PACKED = 4
} wl_codec_card_t;

typedef enum wl_codec_lookup {
  WL_CODEC_LOOKUP_LINEAR = 0,
  WL_CODEC_LOOKUP_DENSE = 1,
  WL_CODEC_LOOKUP_BINARY = 2
} wl_codec_lookup_t;

typedef struct wl_codec_desc wl_codec_desc_t;

typedef void (*wl_codec_clear_fn)(void *msg);
typedef wl_codec_status_t (*wl_codec_measure_fn)(const void *msg,
                                                 size_t *out_size);
typedef wl_codec_status_t (*wl_codec_encode_fn)(const void *msg, uint8_t *out,
                                                size_t out_capacity,
                                                size_t *out_length);
typedef wl_codec_status_t (*wl_codec_decode_fn)(const uint8_t *input,
                                                size_t input_length, void *out);

typedef struct wl_codec_field {
  uint16_t number;
  uint8_t kind;
  uint8_t cardinality;
  uint8_t wire_type;
  uint8_t scalar_size;
  uint8_t is_signed;
  uint8_t key_len;
  uint8_t key[3];
  uint8_t has_default;
  uint16_t presence_offset;
  uint16_t offset;
  uint16_t count_offset;
  uint16_t capacity_offset;
  uint32_t max_length;
  uint32_t element_stride;
  uint64_t default_bits;
  const char *string_default;
  const wl_codec_desc_t *child;
} wl_codec_field_t;

struct wl_codec_desc {
  const wl_codec_field_t *fields;
  size_t field_count;
  size_t struct_size;
  size_t presence_bytes;
  uint16_t min_number;
  uint16_t max_number;
  uint8_t lookup;
  uint8_t has_repeated;
  const uint16_t *dense_map;
  wl_codec_clear_fn clear;
  wl_codec_measure_fn measure;
  wl_codec_encode_fn encode;
  wl_codec_decode_fn decode;
};

WL_HOT_ATTR wl_codec_status_t wl_codec_getv_slow(const uint8_t **cursor,
                                                 const uint8_t *end,
                                                 uint64_t *out);
WL_HOT_ATTR uint8_t *wl_codec_putv_slow(uint8_t *cursor, uint64_t value);
WL_HOT_ATTR size_t wl_codec_vsize_slow(uint64_t value);
WL_HOT_ATTR wl_codec_status_t wl_codec_utf8(const uint8_t *data, size_t length);
WL_HOT_ATTR wl_codec_status_t wl_codec_skip(const uint8_t **cursor,
                                            const uint8_t *end,
                                            uint8_t wire_type);
WL_HOT_ATTR const wl_codec_field_t *
wl_codec_find_field(const wl_codec_desc_t *desc, uint16_t number);
WL_HOT_ATTR void wl_codec_clear(const wl_codec_desc_t *desc, void *msg);
WL_HOT_ATTR wl_codec_status_t wl_codec_measure(const wl_codec_desc_t *desc,
                                               const void *msg,
                                               size_t *out_size);
WL_HOT_ATTR wl_codec_status_t wl_codec_encode(const wl_codec_desc_t *desc,
                                              const void *msg, uint8_t *out,
                                              size_t out_capacity,
                                              size_t *out_length);
WL_HOT_ATTR wl_codec_status_t wl_codec_decode(const wl_codec_desc_t *desc,
                                              const uint8_t *input,
                                              size_t input_length, void *out,
                                              void *scratch);
WL_HOT_ATTR wl_codec_status_t
wl_codec_positional_measure(const wl_codec_desc_t *desc, const void *msg,
                            size_t *out_size);
WL_HOT_ATTR wl_codec_status_t
wl_codec_positional_encode(const wl_codec_desc_t *desc, const void *msg,
                           uint8_t *out, size_t out_capacity,
                           size_t *out_length);
WL_HOT_ATTR wl_codec_status_t
wl_codec_positional_decode(const wl_codec_desc_t *desc, const uint8_t *input,
                           size_t input_length, void *out, void *scratch);

static inline wl_codec_status_t wl_codec_getv(const uint8_t **cursor,
                                              const uint8_t *end,
                                              uint64_t *out) {
  if (*cursor != NULL && end != NULL && *cursor < end &&
      (**cursor & 0x80U) == 0U) {
    *out = *(*cursor)++;
    return WL_CODEC_OK;
  }
  return wl_codec_getv_slow(cursor, end, out);
}

static inline uint8_t *wl_codec_putv(uint8_t *cursor, uint64_t value) {
  if (value < 0x80U) {
    *cursor++ = (uint8_t)value;
    return cursor;
  }
  return wl_codec_putv_slow(cursor, value);
}

static inline size_t wl_codec_vsize(uint64_t value) {
  if (value < 0x80U) {
    return 1U;
  }
  return wl_codec_vsize_slow(value);
}

static inline uint32_t wl_codec_zz32(int32_t value) {
  return ((uint32_t)value << 1U) ^ ((uint32_t)0 - ((uint32_t)value >> 31U));
}

static inline int32_t wl_codec_unzz32(uint32_t value) {
  return (int32_t)((value >> 1U) ^ ((uint32_t)0 - (value & 1U)));
}

static inline uint64_t wl_codec_zz64(int64_t value) {
  return ((uint64_t)value << 1U) ^ ((uint64_t)0 - ((uint64_t)value >> 63U));
}

static inline int64_t wl_codec_unzz64(uint64_t value) {
  return (int64_t)((value >> 1U) ^ ((uint64_t)0 - (value & 1U)));
}

static inline uint32_t wl_codec_get_le32(const uint8_t *in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8U) |
         ((uint32_t)in[2] << 16U) | ((uint32_t)in[3] << 24U);
}

static inline void wl_codec_put_le32(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8U);
  out[2] = (uint8_t)(value >> 16U);
  out[3] = (uint8_t)(value >> 24U);
}

static inline uint64_t wl_codec_get_le64(const uint8_t *in) {
  return (uint64_t)in[0] | ((uint64_t)in[1] << 8U) |
         ((uint64_t)in[2] << 16U) | ((uint64_t)in[3] << 24U) |
         ((uint64_t)in[4] << 32U) | ((uint64_t)in[5] << 40U) |
         ((uint64_t)in[6] << 48U) | ((uint64_t)in[7] << 56U);
}

static inline void wl_codec_put_le64(uint8_t *out, uint64_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8U);
  out[2] = (uint8_t)(value >> 16U);
  out[3] = (uint8_t)(value >> 24U);
  out[4] = (uint8_t)(value >> 32U);
  out[5] = (uint8_t)(value >> 40U);
  out[6] = (uint8_t)(value >> 48U);
  out[7] = (uint8_t)(value >> 56U);
}

#ifdef __cplusplus
}
#endif

#endif
