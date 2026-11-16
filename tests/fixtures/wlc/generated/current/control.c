/* SPDX-License-Identifier: Apache-2.0 */
#include "control.h"
#include <stddef.h>
#include <string.h>

#if JOINT_COMMAND_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if ARM_COMMAND_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if ARM_MIT_COMMAND_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if HOME_REQUEST_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if HOME_RESPONSE_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if BULK_BEGIN_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if BULK_CHUNK_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if BULK_END_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if BULK_ABORT_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif
#if BULK_STATUS_CODEC_PROFILE != 1U
#error "Codec source/header profile mismatch; regenerate the matched artifacts."
#endif

static const wl_codec_desc_t joint_command_desc;
static const wl_codec_desc_t arm_command_desc;
static const wl_codec_desc_t arm_mit_command_desc;
static const wl_codec_desc_t bulk_chunk_desc;

typedef struct wlc_scalar_shape_0_s {
  bool h0;
  uint32_t v0;
  bool h1;
  uint32_t v1;
  bool h2;
  uint32_t v2;
  bool h3;
  uint32_t v3;
  bool h4;
  uint32_t v4;
  bool h5;
  joint_mode_t v5;
} wlc_scalar_shape_0_t;

static void wlc_scalar_shape_0_clear(void *value) {
  if (value == NULL) return;
  wlc_scalar_shape_0_t temp = {0};
  memcpy(value, &temp, sizeof(temp));
}

static size_t wlc_scalar_shape_0_measure_fields(const wlc_scalar_shape_0_t *v) {
  (void)v;
  size_t size = 0U;
  if (v->h0) {
    size += 5U;
  }
  if (v->h1) {
    size += 5U;
  }
  if (v->h2) {
    size += 5U;
  }
  if (v->h3) {
    size += 5U;
  }
  if (v->h4) {
    size += 5U;
  }
  if (v->h5) {
    size += 1U + wl_codec_vsize((uint64_t)wl_codec_zz32(v->v5));
  }
  return size;
}

static size_t wlc_scalar_shape_0_encoded_size(const void *value) {
  if (value == NULL) return SIZE_MAX;
  wlc_scalar_shape_0_t v;
  memcpy(&v, value, sizeof(v));
  return wlc_scalar_shape_0_measure_fields(&v);
}

static wl_codec_status_t wlc_scalar_shape_0_encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_0_t v;
  memcpy(&v, value, sizeof(v));
  if (length == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  if (out == NULL || capacity < 31U) {
    const size_t need = wlc_scalar_shape_0_measure_fields(&v);
    if (need != 0U && out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
    if (capacity < need) return WL_CODEC_ERR_CAPACITY;
    if (need == 0U) { *length = 0U; return WL_CODEC_OK; }
  }
  uint8_t *cursor = out;
  if (v.h0) {
    *cursor++ = 0x0dU;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v0, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  if (v.h1) {
    *cursor++ = 0x15U;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v1, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  if (v.h2) {
    *cursor++ = 0x1dU;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v2, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  if (v.h3) {
    *cursor++ = 0x25U;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v3, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  if (v.h4) {
    *cursor++ = 0x2dU;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v4, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  if (v.h5) {
    *cursor++ = 0x30U;
    cursor = wl_codec_putv(cursor, (uint64_t)wl_codec_zz32(v.v5));
  }
  *length = (size_t)(cursor - out);
  return WL_CODEC_OK;
}

static wl_codec_status_t wlc_scalar_shape_0_decode(const uint8_t *input, size_t length, void *out) {
  if (out == NULL || (length != 0U && input == NULL)) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_0_t temp;
  wlc_scalar_shape_0_clear(&temp);
  uint8_t seen = 0U;
  const uint8_t *cursor = input;
  const uint8_t *end = length != 0U ? input + length : input;
  while (cursor < end) {
    uint64_t wide_key = 0U;
    wl_codec_status_t status = wl_codec_getv(&cursor, end, &wide_key);
    if (status != WL_CODEC_OK) return status;
    const uint64_t number = wide_key >> 3U;
    const uint8_t wire = (uint8_t)(wide_key & 7U);
    if (number == 0U || number > UINT16_MAX || (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    switch ((uint16_t)number) {
      case 1U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 1U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v0, &bits, 4U);
        temp.h0 = true;
        seen |= 1U;
        break;
      }
      case 2U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 2U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v1, &bits, 4U);
        temp.h1 = true;
        seen |= 2U;
        break;
      }
      case 3U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 4U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v2, &bits, 4U);
        temp.h2 = true;
        seen |= 4U;
        break;
      }
      case 4U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 8U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v3, &bits, 4U);
        temp.h3 = true;
        seen |= 8U;
        break;
      }
      case 5U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 16U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v4, &bits, 4U);
        temp.h4 = true;
        seen |= 16U;
        break;
      }
      case 6U: {
        if (wire != 0U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 32U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t w = 0U;
        wl_codec_status_t s = wl_codec_getv(&cursor, end, &w);
        if (s != WL_CODEC_OK) return s;
        if (w > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        const int32_t sv = wl_codec_unzz32((uint32_t)w);
        temp.v5 = (joint_mode_t)sv;
        temp.h5 = true;
        seen |= 32U;
        break;
      }
      default: {
        status = wl_codec_skip(&cursor, end, wire);
        if (status != WL_CODEC_OK) return status;
        break;
      }
    }
  }
  memcpy(out, &temp, sizeof(temp));
  return WL_CODEC_OK;
}

typedef struct wlc_scalar_shape_1_s {
  bool h0;
  uint32_t v0;
  bool h1;
  uint32_t v1;
} wlc_scalar_shape_1_t;

static void wlc_scalar_shape_1_clear(void *value) {
  if (value == NULL) return;
  wlc_scalar_shape_1_t temp = {0};
  memcpy(value, &temp, sizeof(temp));
}

static size_t wlc_scalar_shape_1_measure_fields(const wlc_scalar_shape_1_t *v) {
  (void)v;
  size_t size = 0U;
  if (v->h0) {
    size += 1U + wl_codec_vsize((uint64_t)v->v0);
  }
  if (v->h1) {
    size += 1U + wl_codec_vsize((uint64_t)v->v1);
  }
  return size;
}

static size_t wlc_scalar_shape_1_encoded_size(const void *value) {
  if (value == NULL) return SIZE_MAX;
  wlc_scalar_shape_1_t v;
  memcpy(&v, value, sizeof(v));
  return wlc_scalar_shape_1_measure_fields(&v);
}

static wl_codec_status_t wlc_scalar_shape_1_encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_1_t v;
  memcpy(&v, value, sizeof(v));
  if (length == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  if (out == NULL || capacity < 12U) {
    const size_t need = wlc_scalar_shape_1_measure_fields(&v);
    if (need != 0U && out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
    if (capacity < need) return WL_CODEC_ERR_CAPACITY;
    if (need == 0U) { *length = 0U; return WL_CODEC_OK; }
  }
  uint8_t *cursor = out;
  if (v.h0) {
    *cursor++ = 0x08U;
    cursor = wl_codec_putv(cursor, (uint64_t)v.v0);
  }
  if (v.h1) {
    *cursor++ = 0x10U;
    cursor = wl_codec_putv(cursor, (uint64_t)v.v1);
  }
  *length = (size_t)(cursor - out);
  return WL_CODEC_OK;
}

static wl_codec_status_t wlc_scalar_shape_1_decode(const uint8_t *input, size_t length, void *out) {
  if (out == NULL || (length != 0U && input == NULL)) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_1_t temp;
  wlc_scalar_shape_1_clear(&temp);
  uint8_t seen = 0U;
  const uint8_t *cursor = input;
  const uint8_t *end = length != 0U ? input + length : input;
  while (cursor < end) {
    uint64_t wide_key = 0U;
    wl_codec_status_t status = wl_codec_getv(&cursor, end, &wide_key);
    if (status != WL_CODEC_OK) return status;
    const uint64_t number = wide_key >> 3U;
    const uint8_t wire = (uint8_t)(wide_key & 7U);
    if (number == 0U || number > UINT16_MAX || (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    switch ((uint16_t)number) {
      case 1U: {
        if (wire != 0U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 1U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t w = 0U;
        wl_codec_status_t s = wl_codec_getv(&cursor, end, &w);
        if (s != WL_CODEC_OK) return s;
        if (w > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        temp.v0 = (uint32_t)w;
        temp.h0 = true;
        seen |= 1U;
        break;
      }
      case 2U: {
        if (wire != 0U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 2U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t w = 0U;
        wl_codec_status_t s = wl_codec_getv(&cursor, end, &w);
        if (s != WL_CODEC_OK) return s;
        if (w > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        temp.v1 = (uint32_t)w;
        temp.h1 = true;
        seen |= 2U;
        break;
      }
      default: {
        status = wl_codec_skip(&cursor, end, wire);
        if (status != WL_CODEC_OK) return status;
        break;
      }
    }
  }
  memcpy(out, &temp, sizeof(temp));
  return WL_CODEC_OK;
}

typedef struct wlc_scalar_shape_2_s {
  bool h0;
  uint32_t v0;
  bool h1;
  operation_status_t v1;
} wlc_scalar_shape_2_t;

static void wlc_scalar_shape_2_clear(void *value) {
  if (value == NULL) return;
  wlc_scalar_shape_2_t temp = {0};
  memcpy(value, &temp, sizeof(temp));
}

static size_t wlc_scalar_shape_2_measure_fields(const wlc_scalar_shape_2_t *v) {
  (void)v;
  size_t size = 0U;
  if (v->h0) {
    size += 1U + wl_codec_vsize((uint64_t)v->v0);
  }
  if (v->h1) {
    size += 1U + wl_codec_vsize((uint64_t)wl_codec_zz32(v->v1));
  }
  return size;
}

static size_t wlc_scalar_shape_2_encoded_size(const void *value) {
  if (value == NULL) return SIZE_MAX;
  wlc_scalar_shape_2_t v;
  memcpy(&v, value, sizeof(v));
  return wlc_scalar_shape_2_measure_fields(&v);
}

static wl_codec_status_t wlc_scalar_shape_2_encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_2_t v;
  memcpy(&v, value, sizeof(v));
  if (length == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  if (out == NULL || capacity < 12U) {
    const size_t need = wlc_scalar_shape_2_measure_fields(&v);
    if (need != 0U && out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
    if (capacity < need) return WL_CODEC_ERR_CAPACITY;
    if (need == 0U) { *length = 0U; return WL_CODEC_OK; }
  }
  uint8_t *cursor = out;
  if (v.h0) {
    *cursor++ = 0x08U;
    cursor = wl_codec_putv(cursor, (uint64_t)v.v0);
  }
  if (v.h1) {
    *cursor++ = 0x10U;
    cursor = wl_codec_putv(cursor, (uint64_t)wl_codec_zz32(v.v1));
  }
  *length = (size_t)(cursor - out);
  return WL_CODEC_OK;
}

static wl_codec_status_t wlc_scalar_shape_2_decode(const uint8_t *input, size_t length, void *out) {
  if (out == NULL || (length != 0U && input == NULL)) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_2_t temp;
  wlc_scalar_shape_2_clear(&temp);
  uint8_t seen = 0U;
  const uint8_t *cursor = input;
  const uint8_t *end = length != 0U ? input + length : input;
  while (cursor < end) {
    uint64_t wide_key = 0U;
    wl_codec_status_t status = wl_codec_getv(&cursor, end, &wide_key);
    if (status != WL_CODEC_OK) return status;
    const uint64_t number = wide_key >> 3U;
    const uint8_t wire = (uint8_t)(wide_key & 7U);
    if (number == 0U || number > UINT16_MAX || (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    switch ((uint16_t)number) {
      case 1U: {
        if (wire != 0U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 1U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t w = 0U;
        wl_codec_status_t s = wl_codec_getv(&cursor, end, &w);
        if (s != WL_CODEC_OK) return s;
        if (w > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        temp.v0 = (uint32_t)w;
        temp.h0 = true;
        seen |= 1U;
        break;
      }
      case 2U: {
        if (wire != 0U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 2U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t w = 0U;
        wl_codec_status_t s = wl_codec_getv(&cursor, end, &w);
        if (s != WL_CODEC_OK) return s;
        if (w > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        const int32_t sv = wl_codec_unzz32((uint32_t)w);
        temp.v1 = (operation_status_t)sv;
        temp.h1 = true;
        seen |= 2U;
        break;
      }
      default: {
        status = wl_codec_skip(&cursor, end, wire);
        if (status != WL_CODEC_OK) return status;
        break;
      }
    }
  }
  memcpy(out, &temp, sizeof(temp));
  return WL_CODEC_OK;
}

typedef struct wlc_scalar_shape_3_s {
  bool h0;
  uint32_t v0;
  bool h1;
  uint64_t v1;
  bool h2;
  uint32_t v2;
  bool h3;
  uint32_t v3;
} wlc_scalar_shape_3_t;

static void wlc_scalar_shape_3_clear(void *value) {
  if (value == NULL) return;
  wlc_scalar_shape_3_t temp = {0};
  memcpy(value, &temp, sizeof(temp));
}

static size_t wlc_scalar_shape_3_measure_fields(const wlc_scalar_shape_3_t *v) {
  (void)v;
  size_t size = 0U;
  if (v->h0) {
    size += 5U;
  }
  if (v->h1) {
    size += 9U;
  }
  if (v->h2) {
    size += 5U;
  }
  if (v->h3) {
    size += 5U;
  }
  return size;
}

static size_t wlc_scalar_shape_3_encoded_size(const void *value) {
  if (value == NULL) return SIZE_MAX;
  wlc_scalar_shape_3_t v;
  memcpy(&v, value, sizeof(v));
  return wlc_scalar_shape_3_measure_fields(&v);
}

static wl_codec_status_t wlc_scalar_shape_3_encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_3_t v;
  memcpy(&v, value, sizeof(v));
  if (length == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  if (out == NULL || capacity < 24U) {
    const size_t need = wlc_scalar_shape_3_measure_fields(&v);
    if (need != 0U && out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
    if (capacity < need) return WL_CODEC_ERR_CAPACITY;
    if (need == 0U) { *length = 0U; return WL_CODEC_OK; }
  }
  uint8_t *cursor = out;
  if (v.h0) {
    *cursor++ = 0x0dU;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v0, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  if (v.h1) {
    *cursor++ = 0x11U;
    {
      uint64_t bits = 0U;
      memcpy(&bits, &v.v1, 8U);
      wl_codec_put_le64(cursor, bits);
      cursor += 8U;
    }
  }
  if (v.h2) {
    *cursor++ = 0x1dU;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v2, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  if (v.h3) {
    *cursor++ = 0x25U;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v3, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  *length = (size_t)(cursor - out);
  return WL_CODEC_OK;
}

static wl_codec_status_t wlc_scalar_shape_3_decode(const uint8_t *input, size_t length, void *out) {
  if (out == NULL || (length != 0U && input == NULL)) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_3_t temp;
  wlc_scalar_shape_3_clear(&temp);
  uint8_t seen = 0U;
  const uint8_t *cursor = input;
  const uint8_t *end = length != 0U ? input + length : input;
  while (cursor < end) {
    uint64_t wide_key = 0U;
    wl_codec_status_t status = wl_codec_getv(&cursor, end, &wide_key);
    if (status != WL_CODEC_OK) return status;
    const uint64_t number = wide_key >> 3U;
    const uint8_t wire = (uint8_t)(wide_key & 7U);
    if (number == 0U || number > UINT16_MAX || (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    switch ((uint16_t)number) {
      case 1U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 1U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v0, &bits, 4U);
        temp.h0 = true;
        seen |= 1U;
        break;
      }
      case 2U: {
        if (wire != 1U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 2U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 8U) return WL_CODEC_ERR_MALFORMED;
        const uint64_t bits = wl_codec_get_le64(cursor);
        cursor += 8U;
        memcpy(&temp.v1, &bits, 8U);
        temp.h1 = true;
        seen |= 2U;
        break;
      }
      case 3U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 4U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v2, &bits, 4U);
        temp.h2 = true;
        seen |= 4U;
        break;
      }
      case 4U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 8U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v3, &bits, 4U);
        temp.h3 = true;
        seen |= 8U;
        break;
      }
      default: {
        status = wl_codec_skip(&cursor, end, wire);
        if (status != WL_CODEC_OK) return status;
        break;
      }
    }
  }
  memcpy(out, &temp, sizeof(temp));
  return WL_CODEC_OK;
}

typedef struct wlc_scalar_shape_4_s {
  bool h0;
  uint32_t v0;
  bool h1;
  uint64_t v1;
  bool h2;
  uint32_t v2;
} wlc_scalar_shape_4_t;

static void wlc_scalar_shape_4_clear(void *value) {
  if (value == NULL) return;
  wlc_scalar_shape_4_t temp = {0};
  memcpy(value, &temp, sizeof(temp));
}

static size_t wlc_scalar_shape_4_measure_fields(const wlc_scalar_shape_4_t *v) {
  (void)v;
  size_t size = 0U;
  if (v->h0) {
    size += 5U;
  }
  if (v->h1) {
    size += 9U;
  }
  if (v->h2) {
    size += 5U;
  }
  return size;
}

static size_t wlc_scalar_shape_4_encoded_size(const void *value) {
  if (value == NULL) return SIZE_MAX;
  wlc_scalar_shape_4_t v;
  memcpy(&v, value, sizeof(v));
  return wlc_scalar_shape_4_measure_fields(&v);
}

static wl_codec_status_t wlc_scalar_shape_4_encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_4_t v;
  memcpy(&v, value, sizeof(v));
  if (length == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  if (out == NULL || capacity < 19U) {
    const size_t need = wlc_scalar_shape_4_measure_fields(&v);
    if (need != 0U && out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
    if (capacity < need) return WL_CODEC_ERR_CAPACITY;
    if (need == 0U) { *length = 0U; return WL_CODEC_OK; }
  }
  uint8_t *cursor = out;
  if (v.h0) {
    *cursor++ = 0x0dU;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v0, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  if (v.h1) {
    *cursor++ = 0x11U;
    {
      uint64_t bits = 0U;
      memcpy(&bits, &v.v1, 8U);
      wl_codec_put_le64(cursor, bits);
      cursor += 8U;
    }
  }
  if (v.h2) {
    *cursor++ = 0x1dU;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v2, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  *length = (size_t)(cursor - out);
  return WL_CODEC_OK;
}

static wl_codec_status_t wlc_scalar_shape_4_decode(const uint8_t *input, size_t length, void *out) {
  if (out == NULL || (length != 0U && input == NULL)) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_4_t temp;
  wlc_scalar_shape_4_clear(&temp);
  uint8_t seen = 0U;
  const uint8_t *cursor = input;
  const uint8_t *end = length != 0U ? input + length : input;
  while (cursor < end) {
    uint64_t wide_key = 0U;
    wl_codec_status_t status = wl_codec_getv(&cursor, end, &wide_key);
    if (status != WL_CODEC_OK) return status;
    const uint64_t number = wide_key >> 3U;
    const uint8_t wire = (uint8_t)(wide_key & 7U);
    if (number == 0U || number > UINT16_MAX || (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    switch ((uint16_t)number) {
      case 1U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 1U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v0, &bits, 4U);
        temp.h0 = true;
        seen |= 1U;
        break;
      }
      case 2U: {
        if (wire != 1U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 2U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 8U) return WL_CODEC_ERR_MALFORMED;
        const uint64_t bits = wl_codec_get_le64(cursor);
        cursor += 8U;
        memcpy(&temp.v1, &bits, 8U);
        temp.h1 = true;
        seen |= 2U;
        break;
      }
      case 3U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 4U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v2, &bits, 4U);
        temp.h2 = true;
        seen |= 4U;
        break;
      }
      default: {
        status = wl_codec_skip(&cursor, end, wire);
        if (status != WL_CODEC_OK) return status;
        break;
      }
    }
  }
  memcpy(out, &temp, sizeof(temp));
  return WL_CODEC_OK;
}

typedef struct wlc_scalar_shape_5_s {
  bool h0;
  uint32_t v0;
  bool h1;
  int32_t v1;
} wlc_scalar_shape_5_t;

static void wlc_scalar_shape_5_clear(void *value) {
  if (value == NULL) return;
  wlc_scalar_shape_5_t temp = {0};
  memcpy(value, &temp, sizeof(temp));
}

static size_t wlc_scalar_shape_5_measure_fields(const wlc_scalar_shape_5_t *v) {
  (void)v;
  size_t size = 0U;
  if (v->h0) {
    size += 5U;
  }
  if (v->h1) {
    size += 1U + wl_codec_vsize((uint64_t)wl_codec_zz32(v->v1));
  }
  return size;
}

static size_t wlc_scalar_shape_5_encoded_size(const void *value) {
  if (value == NULL) return SIZE_MAX;
  wlc_scalar_shape_5_t v;
  memcpy(&v, value, sizeof(v));
  return wlc_scalar_shape_5_measure_fields(&v);
}

static wl_codec_status_t wlc_scalar_shape_5_encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_5_t v;
  memcpy(&v, value, sizeof(v));
  if (length == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  if (out == NULL || capacity < 11U) {
    const size_t need = wlc_scalar_shape_5_measure_fields(&v);
    if (need != 0U && out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
    if (capacity < need) return WL_CODEC_ERR_CAPACITY;
    if (need == 0U) { *length = 0U; return WL_CODEC_OK; }
  }
  uint8_t *cursor = out;
  if (v.h0) {
    *cursor++ = 0x0dU;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v0, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  if (v.h1) {
    *cursor++ = 0x10U;
    cursor = wl_codec_putv(cursor, (uint64_t)wl_codec_zz32(v.v1));
  }
  *length = (size_t)(cursor - out);
  return WL_CODEC_OK;
}

static wl_codec_status_t wlc_scalar_shape_5_decode(const uint8_t *input, size_t length, void *out) {
  if (out == NULL || (length != 0U && input == NULL)) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_5_t temp;
  wlc_scalar_shape_5_clear(&temp);
  uint8_t seen = 0U;
  const uint8_t *cursor = input;
  const uint8_t *end = length != 0U ? input + length : input;
  while (cursor < end) {
    uint64_t wide_key = 0U;
    wl_codec_status_t status = wl_codec_getv(&cursor, end, &wide_key);
    if (status != WL_CODEC_OK) return status;
    const uint64_t number = wide_key >> 3U;
    const uint8_t wire = (uint8_t)(wide_key & 7U);
    if (number == 0U || number > UINT16_MAX || (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    switch ((uint16_t)number) {
      case 1U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 1U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v0, &bits, 4U);
        temp.h0 = true;
        seen |= 1U;
        break;
      }
      case 2U: {
        if (wire != 0U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 2U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t w = 0U;
        wl_codec_status_t s = wl_codec_getv(&cursor, end, &w);
        if (s != WL_CODEC_OK) return s;
        if (w > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        const int32_t sv = wl_codec_unzz32((uint32_t)w);
        temp.v1 = (int32_t)sv;
        temp.h1 = true;
        seen |= 2U;
        break;
      }
      default: {
        status = wl_codec_skip(&cursor, end, wire);
        if (status != WL_CODEC_OK) return status;
        break;
      }
    }
  }
  memcpy(out, &temp, sizeof(temp));
  return WL_CODEC_OK;
}

typedef struct wlc_scalar_shape_6_s {
  bool h0;
  uint32_t v0;
  bool h1;
  control_bulk_phase_t v1;
  bool h2;
  control_bulk_status_code_t v2;
  bool h3;
  uint64_t v3;
  bool h4;
  uint32_t v4;
} wlc_scalar_shape_6_t;

static void wlc_scalar_shape_6_clear(void *value) {
  if (value == NULL) return;
  wlc_scalar_shape_6_t temp = {0};
  memcpy(value, &temp, sizeof(temp));
}

static size_t wlc_scalar_shape_6_measure_fields(const wlc_scalar_shape_6_t *v) {
  (void)v;
  size_t size = 0U;
  if (v->h0) {
    size += 5U;
  }
  if (v->h1) {
    size += 1U + wl_codec_vsize((uint64_t)wl_codec_zz32(v->v1));
  }
  if (v->h2) {
    size += 1U + wl_codec_vsize((uint64_t)wl_codec_zz32(v->v2));
  }
  if (v->h3) {
    size += 9U;
  }
  if (v->h4) {
    size += 5U;
  }
  return size;
}

static size_t wlc_scalar_shape_6_encoded_size(const void *value) {
  if (value == NULL) return SIZE_MAX;
  wlc_scalar_shape_6_t v;
  memcpy(&v, value, sizeof(v));
  return wlc_scalar_shape_6_measure_fields(&v);
}

static wl_codec_status_t wlc_scalar_shape_6_encode(const void *value, uint8_t *out, size_t capacity, size_t *length) {
  if (value == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_6_t v;
  memcpy(&v, value, sizeof(v));
  if (length == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  if (out == NULL || capacity < 31U) {
    const size_t need = wlc_scalar_shape_6_measure_fields(&v);
    if (need != 0U && out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
    if (capacity < need) return WL_CODEC_ERR_CAPACITY;
    if (need == 0U) { *length = 0U; return WL_CODEC_OK; }
  }
  uint8_t *cursor = out;
  if (v.h0) {
    *cursor++ = 0x0dU;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v0, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  if (v.h1) {
    *cursor++ = 0x10U;
    cursor = wl_codec_putv(cursor, (uint64_t)wl_codec_zz32(v.v1));
  }
  if (v.h2) {
    *cursor++ = 0x18U;
    cursor = wl_codec_putv(cursor, (uint64_t)wl_codec_zz32(v.v2));
  }
  if (v.h3) {
    *cursor++ = 0x21U;
    {
      uint64_t bits = 0U;
      memcpy(&bits, &v.v3, 8U);
      wl_codec_put_le64(cursor, bits);
      cursor += 8U;
    }
  }
  if (v.h4) {
    *cursor++ = 0x2dU;
    {
      uint32_t bits = 0U;
      memcpy(&bits, &v.v4, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  }
  *length = (size_t)(cursor - out);
  return WL_CODEC_OK;
}

static wl_codec_status_t wlc_scalar_shape_6_decode(const uint8_t *input, size_t length, void *out) {
  if (out == NULL || (length != 0U && input == NULL)) return WL_CODEC_ERR_INVALID_VALUE;
  wlc_scalar_shape_6_t temp;
  wlc_scalar_shape_6_clear(&temp);
  uint8_t seen = 0U;
  const uint8_t *cursor = input;
  const uint8_t *end = length != 0U ? input + length : input;
  while (cursor < end) {
    uint64_t wide_key = 0U;
    wl_codec_status_t status = wl_codec_getv(&cursor, end, &wide_key);
    if (status != WL_CODEC_OK) return status;
    const uint64_t number = wide_key >> 3U;
    const uint8_t wire = (uint8_t)(wide_key & 7U);
    if (number == 0U || number > UINT16_MAX || (wire != 0U && wire != 1U && wire != 2U && wire != 5U))
      return WL_CODEC_ERR_MALFORMED;
    switch ((uint16_t)number) {
      case 1U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 1U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v0, &bits, 4U);
        temp.h0 = true;
        seen |= 1U;
        break;
      }
      case 2U: {
        if (wire != 0U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 2U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t w = 0U;
        wl_codec_status_t s = wl_codec_getv(&cursor, end, &w);
        if (s != WL_CODEC_OK) return s;
        if (w > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        const int32_t sv = wl_codec_unzz32((uint32_t)w);
        temp.v1 = (control_bulk_phase_t)sv;
        temp.h1 = true;
        seen |= 2U;
        break;
      }
      case 3U: {
        if (wire != 0U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 4U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        uint64_t w = 0U;
        wl_codec_status_t s = wl_codec_getv(&cursor, end, &w);
        if (s != WL_CODEC_OK) return s;
        if (w > UINT32_MAX) return WL_CODEC_ERR_OVERFLOW;
        const int32_t sv = wl_codec_unzz32((uint32_t)w);
        temp.v2 = (control_bulk_status_code_t)sv;
        temp.h2 = true;
        seen |= 4U;
        break;
      }
      case 4U: {
        if (wire != 1U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 8U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 8U) return WL_CODEC_ERR_MALFORMED;
        const uint64_t bits = wl_codec_get_le64(cursor);
        cursor += 8U;
        memcpy(&temp.v3, &bits, 8U);
        temp.h3 = true;
        seen |= 8U;
        break;
      }
      case 5U: {
        if (wire != 5U) return WL_CODEC_ERR_WIRE_TYPE;
        if ((seen & 16U) != 0U) return WL_CODEC_ERR_DUPLICATE_FIELD;
        if ((size_t)(end - cursor) < 4U) return WL_CODEC_ERR_MALFORMED;
        const uint32_t bits = wl_codec_get_le32(cursor);
        cursor += 4U;
        memcpy(&temp.v4, &bits, 4U);
        temp.h4 = true;
        seen |= 16U;
        break;
      }
      default: {
        status = wl_codec_skip(&cursor, end, wire);
        if (status != WL_CODEC_OK) return status;
        break;
      }
    }
  }
  memcpy(out, &temp, sizeof(temp));
  return WL_CODEC_OK;
}

void joint_command_clear(joint_command_t *value) {
  _Static_assert(sizeof(joint_command_t) == sizeof(wlc_scalar_shape_0_t), "shape layout mismatch");
  wlc_scalar_shape_0_clear(value);
}
size_t joint_command_encoded_size(const joint_command_t *value) { return wlc_scalar_shape_0_encoded_size(value); }
wl_codec_status_t joint_command_encode(const joint_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_0_encode(value, out, out_capacity, out_length); }
wl_codec_status_t joint_command_decode(const uint8_t *input, size_t input_length, joint_command_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  joint_command_t temp;
  wl_codec_status_t status = wlc_scalar_shape_0_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}

static void joint_command_wlc_clear_v(void *value) { joint_command_clear(value); }
static wl_codec_status_t joint_command_wlc_measure_v(const void *value, size_t *out_size) {
  const size_t size = joint_command_encoded_size(value);
  if (size == SIZE_MAX) return WL_CODEC_ERR_INVALID_VALUE;
  *out_size = size;
  return WL_CODEC_OK;
}
static wl_codec_status_t joint_command_wlc_encode_v(const void *value, uint8_t *out, size_t capacity, size_t *length) { return joint_command_encode(value, out, capacity, length); }
static wl_codec_status_t joint_command_wlc_decode_v(const uint8_t *input, size_t length, void *out) { return joint_command_decode(input, length, out); }
static const wl_codec_desc_t joint_command_desc = {
  .fields = NULL,
  .field_count = 0U,
  .struct_size = sizeof(joint_command_t),
  .presence_bytes = 0U,
  .min_number = 1U,
  .max_number = 6U,
  .lookup = WL_CODEC_LOOKUP_LINEAR,
  .has_repeated = 0U,
  .dense_map = NULL,
  .clear = joint_command_wlc_clear_v,
  .measure = joint_command_wlc_measure_v,
  .encode = joint_command_wlc_encode_v,
  .decode = joint_command_wlc_decode_v
};

void arm_command_clear(arm_command_t *value) { wl_codec_clear(&arm_command_desc, value); }
size_t arm_command_encoded_size(const arm_command_t *value) { size_t size = 0U; return wl_codec_measure(&arm_command_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t arm_command_encode(const arm_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&arm_command_desc, value, out, out_capacity, out_length); }
wl_codec_status_t arm_command_decode(const uint8_t *input, size_t input_length, arm_command_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; arm_command_t scratch; return wl_codec_decode(&arm_command_desc, input, input_length, out, &scratch); }

static const char arm_command_source_default_str[] = "\x68""\x6F""\x73""\x74";
static const wl_codec_field_t arm_command_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_MESSAGE, .cardinality = WL_CODEC_CARD_REPEATED, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = 0U, .offset = (uint16_t)offsetof(arm_command_t, joints), .count_offset = (uint16_t)offsetof(arm_command_t, joints_count), .capacity_offset = (uint16_t)offsetof(arm_command_t, joints_capacity), .max_length = 0U, .element_stride = (uint32_t)sizeof(*(((arm_command_t *)0)->joints)), .default_bits = UINT64_C(0), .string_default = NULL, .child = &joint_command_desc },
  { .number = 2U, .kind = WL_CODEC_KIND_UINT64, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 8U, .is_signed = 0U, .key_len = 1U, .key = { 0x10U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(arm_command_t, has_sequence), .offset = (uint16_t)offsetof(arm_command_t, sequence), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 3U, .kind = WL_CODEC_KIND_STRING, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x1aU, 0x00U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(arm_command_t, has_source), .offset = (uint16_t)offsetof(arm_command_t, source), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(4), .string_default = arm_command_source_default_str, .child = NULL },
  { .number = 4U, .kind = WL_CODEC_KIND_BYTES, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x22U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(arm_command_t, has_extension), .offset = (uint16_t)offsetof(arm_command_t, extension), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 5U, .kind = WL_CODEC_KIND_BOOL, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 1U, .is_signed = 0U, .key_len = 1U, .key = { 0x28U, 0x00U, 0x00U }, .has_default = 1U, .presence_offset = (uint16_t)offsetof(arm_command_t, has_enabled), .offset = (uint16_t)offsetof(arm_command_t, enabled), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(1), .string_default = NULL, .child = NULL },
};
static const wl_codec_desc_t arm_command_desc = {
  .fields = arm_command_fields,
  .field_count = 5U,
  .struct_size = sizeof(arm_command_t),
  .presence_bytes = 0U,
  .min_number = 1U,
  .max_number = 5U,
  .lookup = WL_CODEC_LOOKUP_LINEAR,
  .has_repeated = 1U,
  .dense_map = NULL,
  .clear = NULL,
  .measure = NULL,
  .encode = NULL,
  .decode = NULL
};

void arm_mit_command_clear(arm_mit_command_t *value) { wl_codec_clear(&arm_mit_command_desc, value); }
size_t arm_mit_command_encoded_size(const arm_mit_command_t *value) { size_t size = 0U; return wl_codec_measure(&arm_mit_command_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t arm_mit_command_encode(const arm_mit_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&arm_mit_command_desc, value, out, out_capacity, out_length); }
wl_codec_status_t arm_mit_command_decode(const uint8_t *input, size_t input_length, arm_mit_command_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; arm_mit_command_t scratch; return wl_codec_decode(&arm_mit_command_desc, input, input_length, out, &scratch); }

static const wl_codec_field_t arm_mit_command_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_FLOAT32, .cardinality = WL_CODEC_CARD_PACKED, .wire_type = 2U, .scalar_size = 4U, .is_signed = 0U, .key_len = 1U, .key = { 0x0aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(arm_mit_command_t, has_controls), .offset = (uint16_t)offsetof(arm_mit_command_t, controls), .count_offset = 0U, .capacity_offset = 0U, .max_length = 30U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 2U, .kind = WL_CODEC_KIND_UINT64, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 8U, .is_signed = 0U, .key_len = 1U, .key = { 0x10U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(arm_mit_command_t, has_sequence), .offset = (uint16_t)offsetof(arm_mit_command_t, sequence), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 3U, .kind = WL_CODEC_KIND_FLOAT32, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 5U, .scalar_size = 4U, .is_signed = 0U, .key_len = 1U, .key = { 0x1dU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(arm_mit_command_t, has_dt_s), .offset = (uint16_t)offsetof(arm_mit_command_t, dt_s), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
};
static const wl_codec_desc_t arm_mit_command_desc = {
  .fields = arm_mit_command_fields,
  .field_count = 3U,
  .struct_size = sizeof(arm_mit_command_t),
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

void home_request_clear(home_request_t *value) {
  _Static_assert(sizeof(home_request_t) == sizeof(wlc_scalar_shape_1_t), "shape layout mismatch");
  wlc_scalar_shape_1_clear(value);
}
size_t home_request_encoded_size(const home_request_t *value) { return wlc_scalar_shape_1_encoded_size(value); }
wl_codec_status_t home_request_encode(const home_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_1_encode(value, out, out_capacity, out_length); }
wl_codec_status_t home_request_decode(const uint8_t *input, size_t input_length, home_request_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  home_request_t temp;
  wl_codec_status_t status = wlc_scalar_shape_1_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}

void home_response_clear(home_response_t *value) {
  _Static_assert(sizeof(home_response_t) == sizeof(wlc_scalar_shape_2_t), "shape layout mismatch");
  wlc_scalar_shape_2_clear(value);
}
size_t home_response_encoded_size(const home_response_t *value) { return wlc_scalar_shape_2_encoded_size(value); }
wl_codec_status_t home_response_encode(const home_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_2_encode(value, out, out_capacity, out_length); }
wl_codec_status_t home_response_decode(const uint8_t *input, size_t input_length, home_response_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  home_response_t temp;
  wl_codec_status_t status = wlc_scalar_shape_2_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}

void bulk_begin_clear(bulk_begin_t *value) {
  _Static_assert(sizeof(bulk_begin_t) == sizeof(wlc_scalar_shape_3_t), "shape layout mismatch");
  wlc_scalar_shape_3_clear(value);
}
size_t bulk_begin_encoded_size(const bulk_begin_t *value) { return wlc_scalar_shape_3_encoded_size(value); }
wl_codec_status_t bulk_begin_encode(const bulk_begin_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_3_encode(value, out, out_capacity, out_length); }
wl_codec_status_t bulk_begin_decode(const uint8_t *input, size_t input_length, bulk_begin_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  bulk_begin_t temp;
  wl_codec_status_t status = wlc_scalar_shape_3_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}

void bulk_chunk_clear(bulk_chunk_t *value) { wl_codec_clear(&bulk_chunk_desc, value); }
size_t bulk_chunk_encoded_size(const bulk_chunk_t *value) { size_t size = 0U; return wl_codec_measure(&bulk_chunk_desc, value, &size) == WL_CODEC_OK ? size : SIZE_MAX; }
wl_codec_status_t bulk_chunk_encode(const bulk_chunk_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wl_codec_encode(&bulk_chunk_desc, value, out, out_capacity, out_length); }
wl_codec_status_t bulk_chunk_decode(const uint8_t *input, size_t input_length, bulk_chunk_t *out) { if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE; bulk_chunk_t scratch; return wl_codec_decode(&bulk_chunk_desc, input, input_length, out, &scratch); }

static const wl_codec_field_t bulk_chunk_fields[] = {
  { .number = 1U, .kind = WL_CODEC_KIND_FIXED32, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 5U, .scalar_size = 4U, .is_signed = 0U, .key_len = 1U, .key = { 0x0dU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(bulk_chunk_t, has_transfer_id), .offset = (uint16_t)offsetof(bulk_chunk_t, transfer_id), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 2U, .kind = WL_CODEC_KIND_FIXED64, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 1U, .scalar_size = 8U, .is_signed = 0U, .key_len = 1U, .key = { 0x11U, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(bulk_chunk_t, has_offset), .offset = (uint16_t)offsetof(bulk_chunk_t, offset), .count_offset = 0U, .capacity_offset = 0U, .max_length = 0U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
  { .number = 3U, .kind = WL_CODEC_KIND_INLINE_BYTES, .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 2U, .scalar_size = 0U, .is_signed = 0U, .key_len = 1U, .key = { 0x1aU, 0x00U, 0x00U }, .has_default = 0U, .presence_offset = (uint16_t)offsetof(bulk_chunk_t, has_data), .offset = (uint16_t)offsetof(bulk_chunk_t, data), .count_offset = (uint16_t)(offsetof(bulk_chunk_t, data.data) - offsetof(bulk_chunk_t, data)), .capacity_offset = 0U, .max_length = 4096U, .element_stride = 0U, .default_bits = UINT64_C(0), .string_default = NULL, .child = NULL },
};
static const wl_codec_desc_t bulk_chunk_desc = {
  .fields = bulk_chunk_fields,
  .field_count = 3U,
  .struct_size = sizeof(bulk_chunk_t),
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

void bulk_end_clear(bulk_end_t *value) {
  _Static_assert(sizeof(bulk_end_t) == sizeof(wlc_scalar_shape_4_t), "shape layout mismatch");
  wlc_scalar_shape_4_clear(value);
}
size_t bulk_end_encoded_size(const bulk_end_t *value) { return wlc_scalar_shape_4_encoded_size(value); }
wl_codec_status_t bulk_end_encode(const bulk_end_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_4_encode(value, out, out_capacity, out_length); }
wl_codec_status_t bulk_end_decode(const uint8_t *input, size_t input_length, bulk_end_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  bulk_end_t temp;
  wl_codec_status_t status = wlc_scalar_shape_4_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}

void bulk_abort_clear(bulk_abort_t *value) {
  _Static_assert(sizeof(bulk_abort_t) == sizeof(wlc_scalar_shape_5_t), "shape layout mismatch");
  wlc_scalar_shape_5_clear(value);
}
size_t bulk_abort_encoded_size(const bulk_abort_t *value) { return wlc_scalar_shape_5_encoded_size(value); }
wl_codec_status_t bulk_abort_encode(const bulk_abort_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_5_encode(value, out, out_capacity, out_length); }
wl_codec_status_t bulk_abort_decode(const uint8_t *input, size_t input_length, bulk_abort_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  bulk_abort_t temp;
  wl_codec_status_t status = wlc_scalar_shape_5_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}

void bulk_status_clear(bulk_status_t *value) {
  _Static_assert(sizeof(bulk_status_t) == sizeof(wlc_scalar_shape_6_t), "shape layout mismatch");
  wlc_scalar_shape_6_clear(value);
}
size_t bulk_status_encoded_size(const bulk_status_t *value) { return wlc_scalar_shape_6_encoded_size(value); }
wl_codec_status_t bulk_status_encode(const bulk_status_t *value, uint8_t *out, size_t out_capacity, size_t *out_length) { return wlc_scalar_shape_6_encode(value, out, out_capacity, out_length); }
wl_codec_status_t bulk_status_decode(const uint8_t *input, size_t input_length, bulk_status_t *out) {
  if (out == NULL) return WL_CODEC_ERR_INVALID_VALUE;
  bulk_status_t temp;
  wl_codec_status_t status = wlc_scalar_shape_6_decode(input, input_length, &temp);
  if (status == WL_CODEC_OK) memcpy(out, &temp, sizeof(temp));
  return status;
}


#include <limits.h>
