/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/codec.h"

#include <string.h>

static wl_codec_status_t add_size(size_t *total, size_t add) {
  if (*total > SIZE_MAX - add) {
    return WL_CODEC_ERR_OVERFLOW;
  }
  *total += add;
  return WL_CODEC_OK;
}

static bool is_required_card(uint8_t card) {
  return card == WL_CODEC_CARD_REQUIRED ||
         card == WL_CODEC_CARD_REQUIRED_PACKED;
}

static bool is_packed_card(uint8_t card) {
  return card == WL_CODEC_CARD_PACKED ||
         card == WL_CODEC_CARD_REQUIRED_PACKED;
}

static const uint8_t *inline_cdata(const void *field,
                                   const wl_codec_field_t *f) {
  return (const uint8_t *)field + f->count_offset;
}

static uint8_t *inline_data(void *field, const wl_codec_field_t *f) {
  return (uint8_t *)field + f->count_offset;
}

WL_HOT_ATTR wl_codec_status_t wl_codec_getv_slow(const uint8_t **cursor,
                                                 const uint8_t *end,
                                                 uint64_t *out) {
  if (cursor == NULL || *cursor == NULL || end == NULL) {
    return WL_CODEC_ERR_MALFORMED;
  }
  const uint8_t *p = *cursor;
  uint64_t acc = 0U;
  for (unsigned i = 0U; i < 10U; ++i) {
    if (p >= end) {
      *cursor = p;
      return WL_CODEC_ERR_MALFORMED;
    }
    const uint8_t byte = *p++;
    if (i == 9U && byte > 1U) {
      *cursor = p;
      return WL_CODEC_ERR_OVERFLOW;
    }
    acc |= (uint64_t)(byte & 0x7FU) << (i * 7U);
    if ((byte & 0x80U) == 0U) {
      *cursor = p;
      if (i > 0U && byte == 0U) {
        return WL_CODEC_ERR_MALFORMED;
      }
      *out = acc;
      return WL_CODEC_OK;
    }
  }
  *cursor = p;
  return WL_CODEC_ERR_OVERFLOW;
}

WL_HOT_ATTR uint8_t *wl_codec_putv_slow(uint8_t *cursor, uint64_t value) {
  while (value >= 0x80U) {
    *cursor++ = (uint8_t)((value & 0x7FU) | 0x80U);
    value >>= 7U;
  }
  *cursor++ = (uint8_t)value;
  return cursor;
}

WL_HOT_ATTR size_t wl_codec_vsize_slow(uint64_t value) {
  size_t bytes = 1U;
  while (value >= 0x80U) {
    value >>= 7U;
    ++bytes;
  }
  return bytes;
}

WL_HOT_ATTR wl_codec_status_t wl_codec_utf8(const uint8_t *data, size_t length) {
  if (length == 0U) {
    return WL_CODEC_OK;
  }
  if (data == NULL) {
    return WL_CODEC_ERR_INVALID_VALUE;
  }
  for (size_t i = 0U; i < length;) {
    const uint8_t b0 = data[i];
    if (b0 < 0x80U) {
      ++i;
      continue;
    }
    if (b0 < 0xC2U || b0 > 0xF4U) {
      return WL_CODEC_ERR_UTF8;
    }
    if (b0 < 0xE0U) {
      if (length - i < 2U || (data[i + 1U] & 0xC0U) != 0x80U) {
        return WL_CODEC_ERR_UTF8;
      }
      i += 2U;
    } else if (b0 < 0xF0U) {
      if (length - i < 3U) {
        return WL_CODEC_ERR_UTF8;
      }
      const uint8_t b1 = data[i + 1U];
      const uint8_t b2 = data[i + 2U];
      if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U) {
        return WL_CODEC_ERR_UTF8;
      }
      if ((b0 == 0xE0U && b1 < 0xA0U) || (b0 == 0xEDU && b1 >= 0xA0U)) {
        return WL_CODEC_ERR_UTF8;
      }
      i += 3U;
    } else {
      if (length - i < 4U) {
        return WL_CODEC_ERR_UTF8;
      }
      const uint8_t b1 = data[i + 1U];
      const uint8_t b2 = data[i + 2U];
      const uint8_t b3 = data[i + 3U];
      if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U ||
          (b3 & 0xC0U) != 0x80U) {
        return WL_CODEC_ERR_UTF8;
      }
      if ((b0 == 0xF0U && b1 < 0x90U) || (b0 == 0xF4U && b1 > 0x8FU)) {
        return WL_CODEC_ERR_UTF8;
      }
      i += 4U;
    }
  }
  return WL_CODEC_OK;
}

WL_HOT_ATTR wl_codec_status_t wl_codec_skip(const uint8_t **cursor,
                                            const uint8_t *end,
                                            uint8_t wire_type) {
  switch (wire_type) {
  case 0U: {
    uint64_t ignored = 0U;
    return wl_codec_getv(cursor, end, &ignored);
  }
  case 1U:
    if ((size_t)(end - *cursor) < 8U) {
      return WL_CODEC_ERR_MALFORMED;
    }
    *cursor += 8U;
    return WL_CODEC_OK;
  case 2U: {
    uint64_t bytes = 0U;
    wl_codec_status_t status = wl_codec_getv(cursor, end, &bytes);
    if (status != WL_CODEC_OK) {
      return status;
    }
    if (bytes > (uint64_t)SIZE_MAX) {
      return WL_CODEC_ERR_OVERFLOW;
    }
    if ((size_t)bytes > (size_t)(end - *cursor)) {
      return WL_CODEC_ERR_MALFORMED;
    }
    *cursor += (size_t)bytes;
    return WL_CODEC_OK;
  }
  case 5U:
    if ((size_t)(end - *cursor) < 4U) {
      return WL_CODEC_ERR_MALFORMED;
    }
    *cursor += 4U;
    return WL_CODEC_OK;
  default:
    return WL_CODEC_ERR_WIRE_TYPE;
  }
}

WL_HOT_ATTR const wl_codec_field_t *
wl_codec_find_field(const wl_codec_desc_t *desc, uint16_t number) {
  if (desc->field_count == 0U || number < desc->min_number ||
      number > desc->max_number) {
    return NULL;
  }
  if (desc->lookup == WL_CODEC_LOOKUP_DENSE && desc->dense_map != NULL) {
    const uint16_t index = desc->dense_map[number - desc->min_number];
    return index == UINT16_MAX ? NULL : &desc->fields[index];
  }
  if (desc->lookup == WL_CODEC_LOOKUP_BINARY) {
    size_t low = 0U;
    size_t high = desc->field_count;
    while (low < high) {
      const size_t mid = low + (high - low) / 2U;
      const uint16_t mid_number = desc->fields[mid].number;
      if (mid_number < number) {
        low = mid + 1U;
      } else if (mid_number > number) {
        high = mid;
      } else {
        return &desc->fields[mid];
      }
    }
    return NULL;
  }
  for (size_t i = 0U; i < desc->field_count; ++i) {
    if (desc->fields[i].number == number) {
      return &desc->fields[i];
    }
    if (desc->fields[i].number > number) {
      break;
    }
  }
  return NULL;
}

static void write_uint_default(void *dst, uint8_t scalar_size, uint64_t val) {
  switch (scalar_size) {
  case 1U:
    *(uint8_t *)dst = (uint8_t)val;
    break;
  case 2U:
    *(uint16_t *)dst = (uint16_t)val;
    break;
  case 4U:
    *(uint32_t *)dst = (uint32_t)val;
    break;
  default:
    *(uint64_t *)dst = val;
    break;
  }
}

static void write_sint_default(void *dst, uint8_t scalar_size, int64_t val) {
  switch (scalar_size) {
  case 1U:
    *(int8_t *)dst = (int8_t)val;
    break;
  case 2U:
    *(int16_t *)dst = (int16_t)val;
    break;
  case 4U:
    *(int32_t *)dst = (int32_t)val;
    break;
  default:
    *(int64_t *)dst = val;
    break;
  }
}

static void clear_single_field(uint8_t *base, const wl_codec_field_t *f) {
  void *dst = base + f->offset;
  if (f->cardinality == WL_CODEC_CARD_REPEATED) {
    *(size_t *)(base + f->count_offset) = 0U;
    return;
  }
  if (!is_required_card(f->cardinality))
    *(bool *)(base + f->presence_offset) = false;
  if (is_packed_card(f->cardinality)) {
    memset(dst, 0, (size_t)f->max_length * (size_t)f->scalar_size);
    return;
  }
  switch (f->kind) {
  case WL_CODEC_KIND_BOOL:
    *(bool *)dst = f->has_default != 0U && f->default_bits != 0U;
    break;
  case WL_CODEC_KIND_UINT32:
  case WL_CODEC_KIND_UINT64:
  case WL_CODEC_KIND_FIXED32:
  case WL_CODEC_KIND_FIXED64:
    if (f->has_default != 0U) {
      write_uint_default(dst, f->scalar_size, f->default_bits);
    } else {
      memset(dst, 0, f->scalar_size);
    }
    break;
  case WL_CODEC_KIND_INT32:
  case WL_CODEC_KIND_SINT32:
  case WL_CODEC_KIND_INT64:
  case WL_CODEC_KIND_SINT64:
  case WL_CODEC_KIND_ENUM:
    if (f->has_default != 0U) {
      write_sint_default(dst, f->scalar_size, (int64_t)f->default_bits);
    } else {
      memset(dst, 0, f->scalar_size);
    }
    break;
  case WL_CODEC_KIND_FLOAT32: {
    const uint32_t bits =
        f->has_default != 0U ? (uint32_t)f->default_bits : 0U;
    memcpy(dst, &bits, 4U);
    break;
  }
  case WL_CODEC_KIND_FLOAT64: {
    const uint64_t bits = f->has_default != 0U ? f->default_bits : 0U;
    memcpy(dst, &bits, 8U);
    break;
  }
  case WL_CODEC_KIND_BYTES:
    *(wl_codec_bytes_t *)dst = (wl_codec_bytes_t){NULL, 0U};
    break;
  case WL_CODEC_KIND_STRING:
    if (f->has_default != 0U && f->string_default != NULL) {
      *(wl_codec_string_t *)dst =
          (wl_codec_string_t){f->string_default, (size_t)f->default_bits};
    } else {
      *(wl_codec_string_t *)dst = (wl_codec_string_t){NULL, 0U};
    }
    break;
  case WL_CODEC_KIND_INLINE_BYTES:
    *(size_t *)dst = 0U;
    break;
  case WL_CODEC_KIND_INLINE_STRING: {
    uint8_t *buf = inline_data(dst, f);
    if (f->has_default != 0U && f->string_default != NULL) {
      const size_t def_len = (size_t)f->default_bits;
      *(size_t *)dst = def_len;
      if (def_len > 0U) {
        memcpy(buf, f->string_default, def_len);
      }
      buf[def_len] = '\0';
    } else {
      *(size_t *)dst = 0U;
      buf[0] = '\0';
    }
    break;
  }
  case WL_CODEC_KIND_MESSAGE:
    if (f->child != NULL && f->child->clear != NULL) {
      f->child->clear(dst);
    }
    break;
  default:
    break;
  }
}

WL_HOT_ATTR void wl_codec_clear(const wl_codec_desc_t *desc, void *msg) {
  if (desc == NULL || msg == NULL) {
    return;
  }
  if (desc->field_count == 0U) {
    memset(msg, 0, desc->struct_size);
    return;
  }
  uint8_t *base = msg;
  for (size_t i = 0U; i < desc->field_count; ++i) {
    clear_single_field(base, &desc->fields[i]);
  }
}

static uint64_t read_unsigned(const void *src, uint8_t scalar_size) {
  switch (scalar_size) {
  case 1U:
    return *(const uint8_t *)src;
  case 2U:
    return *(const uint16_t *)src;
  case 4U:
    return *(const uint32_t *)src;
  default:
    return *(const uint64_t *)src;
  }
}

static int64_t read_signed(const void *src, uint8_t scalar_size) {
  switch (scalar_size) {
  case 1U:
    return *(const int8_t *)src;
  case 2U:
    return *(const int16_t *)src;
  case 4U:
    return *(const int32_t *)src;
  default:
    return *(const int64_t *)src;
  }
}

static wl_codec_status_t measure_value(const wl_codec_field_t *f,
                                       const void *src, size_t *out_bytes) {
  switch (f->kind) {
  case WL_CODEC_KIND_BOOL:
    *out_bytes = 1U;
    return WL_CODEC_OK;
  case WL_CODEC_KIND_UINT32:
  case WL_CODEC_KIND_UINT64:
    *out_bytes = wl_codec_vsize(read_unsigned(src, f->scalar_size));
    return WL_CODEC_OK;
  case WL_CODEC_KIND_INT32:
  case WL_CODEC_KIND_SINT32:
  case WL_CODEC_KIND_ENUM:
    *out_bytes = wl_codec_vsize(
        (uint64_t)wl_codec_zz32((int32_t)read_signed(src, f->scalar_size)));
    return WL_CODEC_OK;
  case WL_CODEC_KIND_INT64:
  case WL_CODEC_KIND_SINT64:
    *out_bytes = wl_codec_vsize(wl_codec_zz64(read_signed(src, f->scalar_size)));
    return WL_CODEC_OK;
  case WL_CODEC_KIND_FIXED32:
  case WL_CODEC_KIND_FLOAT32:
    *out_bytes = 4U;
    return WL_CODEC_OK;
  case WL_CODEC_KIND_FIXED64:
  case WL_CODEC_KIND_FLOAT64:
    *out_bytes = 8U;
    return WL_CODEC_OK;
  case WL_CODEC_KIND_BYTES: {
    const wl_codec_bytes_t *b = src;
    if (b->length > 0U && b->data == NULL) {
      return WL_CODEC_ERR_INVALID_VALUE;
    }
    if (f->max_length != 0U && b->length > (size_t)f->max_length) {
      return WL_CODEC_ERR_INVALID_VALUE;
    }
    const size_t len_prefix = wl_codec_vsize((uint64_t)b->length);
    if (b->length > SIZE_MAX - len_prefix) {
      return WL_CODEC_ERR_OVERFLOW;
    }
    *out_bytes = len_prefix + b->length;
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_STRING: {
    const wl_codec_string_t *s = src;
    if (s->length > 0U && s->data == NULL) {
      return WL_CODEC_ERR_INVALID_VALUE;
    }
    if (f->max_length != 0U && s->length > (size_t)f->max_length) {
      return WL_CODEC_ERR_INVALID_VALUE;
    }
    wl_codec_status_t status = wl_codec_utf8((const uint8_t *)s->data, s->length);
    if (status != WL_CODEC_OK) {
      return status;
    }
    const size_t len_prefix = wl_codec_vsize((uint64_t)s->length);
    if (s->length > SIZE_MAX - len_prefix) {
      return WL_CODEC_ERR_OVERFLOW;
    }
    *out_bytes = len_prefix + s->length;
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_INLINE_BYTES: {
    const size_t len = *(const size_t *)src;
    if (len > (size_t)f->max_length) {
      return WL_CODEC_ERR_INVALID_VALUE;
    }
    *out_bytes = wl_codec_vsize((uint64_t)len) + len;
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_INLINE_STRING: {
    const size_t len = *(const size_t *)src;
    if (len > (size_t)f->max_length) {
      return WL_CODEC_ERR_INVALID_VALUE;
    }
    wl_codec_status_t status = wl_codec_utf8(inline_cdata(src, f), len);
    if (status != WL_CODEC_OK) {
      return status;
    }
    *out_bytes = wl_codec_vsize((uint64_t)len) + len;
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_MESSAGE: {
    if (f->child == NULL || f->child->measure == NULL) {
      return WL_CODEC_ERR_INVALID_VALUE;
    }
    size_t child_bytes = 0U;
    wl_codec_status_t status = f->child->measure(src, &child_bytes);
    if (status != WL_CODEC_OK) {
      return status;
    }
    const size_t len_prefix = wl_codec_vsize((uint64_t)child_bytes);
    if (child_bytes > SIZE_MAX - len_prefix) {
      return WL_CODEC_ERR_OVERFLOW;
    }
    *out_bytes = len_prefix + child_bytes;
    return WL_CODEC_OK;
  }
  default:
    return WL_CODEC_ERR_INVALID_VALUE;
  }
}

static uint8_t *emit_value(const wl_codec_field_t *f, const void *src,
                           uint8_t *cursor, wl_codec_status_t *out_status) {
  *out_status = WL_CODEC_OK;
  switch (f->kind) {
  case WL_CODEC_KIND_BOOL:
    *cursor++ = (*(const bool *)src) ? 1U : 0U;
    return cursor;
  case WL_CODEC_KIND_UINT32:
  case WL_CODEC_KIND_UINT64:
    return wl_codec_putv(cursor, read_unsigned(src, f->scalar_size));
  case WL_CODEC_KIND_INT32:
  case WL_CODEC_KIND_SINT32:
  case WL_CODEC_KIND_ENUM:
    return wl_codec_putv(
        cursor, (uint64_t)wl_codec_zz32((int32_t)read_signed(src, f->scalar_size)));
  case WL_CODEC_KIND_INT64:
  case WL_CODEC_KIND_SINT64:
    return wl_codec_putv(cursor,
                         wl_codec_zz64(read_signed(src, f->scalar_size)));
  case WL_CODEC_KIND_FIXED32:
  case WL_CODEC_KIND_FLOAT32: {
    uint32_t bits = 0U;
    memcpy(&bits, src, 4U);
    wl_codec_put_le32(cursor, bits);
    return cursor + 4U;
  }
  case WL_CODEC_KIND_FIXED64:
  case WL_CODEC_KIND_FLOAT64: {
    uint64_t bits = 0U;
    memcpy(&bits, src, 8U);
    wl_codec_put_le64(cursor, bits);
    return cursor + 8U;
  }
  case WL_CODEC_KIND_BYTES: {
    const wl_codec_bytes_t *b = src;
    cursor = wl_codec_putv(cursor, (uint64_t)b->length);
    if (b->length > 0U) {
      memcpy(cursor, b->data, b->length);
      cursor += b->length;
    }
    return cursor;
  }
  case WL_CODEC_KIND_STRING: {
    const wl_codec_string_t *s = src;
    cursor = wl_codec_putv(cursor, (uint64_t)s->length);
    if (s->length > 0U) {
      memcpy(cursor, s->data, s->length);
      cursor += s->length;
    }
    return cursor;
  }
  case WL_CODEC_KIND_INLINE_BYTES:
  case WL_CODEC_KIND_INLINE_STRING: {
    const size_t len = *(const size_t *)src;
    cursor = wl_codec_putv(cursor, (uint64_t)len);
    if (len > 0U) {
      memcpy(cursor, inline_cdata(src, f), len);
      cursor += len;
    }
    return cursor;
  }
  case WL_CODEC_KIND_MESSAGE: {
    size_t child_bytes = 0U;
    wl_codec_status_t status = f->child->measure(src, &child_bytes);
    if (status != WL_CODEC_OK) {
      *out_status = status;
      return cursor;
    }
    cursor = wl_codec_putv(cursor, (uint64_t)child_bytes);
    size_t written = 0U;
    status = f->child->encode(src, cursor, child_bytes, &written);
    if (status != WL_CODEC_OK) {
      *out_status = status;
      return cursor;
    }
    return cursor + written;
  }
  default:
    *out_status = WL_CODEC_ERR_INVALID_VALUE;
    return cursor;
  }
}

static size_t packed_payload_size(const wl_codec_field_t *f) {
  return (size_t)f->max_length * (size_t)f->scalar_size;
}

static uint8_t *emit_packed(const wl_codec_field_t *f, const uint8_t *src,
                            uint8_t *cursor) {
  const size_t payload = packed_payload_size(f);
  cursor = wl_codec_putv(cursor, (uint64_t)payload);
  if (f->scalar_size == 4U) {
    for (uint32_t i = 0U; i < f->max_length; ++i) {
      uint32_t bits = 0U;
      memcpy(&bits, src + (size_t)i * 4U, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
    }
  } else {
    for (uint32_t i = 0U; i < f->max_length; ++i) {
      uint64_t bits = 0U;
      memcpy(&bits, src + (size_t)i * 8U, 8U);
      wl_codec_put_le64(cursor, bits);
      cursor += 8U;
    }
  }
  return cursor;
}

static wl_codec_status_t decode_value(const wl_codec_field_t *f,
                                      const uint8_t **cursor,
                                      const uint8_t *end, void *dst, bool local) {
  switch (f->kind) {
  case WL_CODEC_KIND_BOOL: {
    uint64_t v = 0U;
    wl_codec_status_t s = wl_codec_getv(cursor, end, &v);
    if (s != WL_CODEC_OK) {
      return s;
    }
    if (v > 1U) {
      return WL_CODEC_ERR_INVALID_VALUE;
    }
    *(bool *)dst = (v != 0U);
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_UINT32: {
    uint64_t v = 0U;
    wl_codec_status_t s = wl_codec_getv(cursor, end, &v);
    if (s != WL_CODEC_OK) {
      return s;
    }
    const uint64_t limit = (f->scalar_size == 1U)   ? UINT8_MAX
                           : (f->scalar_size == 2U) ? UINT16_MAX
                                                    : UINT32_MAX;
    if (v > limit) {
      return WL_CODEC_ERR_OVERFLOW;
    }
    write_uint_default(dst, f->scalar_size, v);
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_UINT64: {
    uint64_t v = 0U;
    wl_codec_status_t s = wl_codec_getv(cursor, end, &v);
    if (s != WL_CODEC_OK) {
      return s;
    }
    *(uint64_t *)dst = v;
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_INT32:
  case WL_CODEC_KIND_SINT32:
  case WL_CODEC_KIND_ENUM: {
    uint64_t v = 0U;
    wl_codec_status_t s = wl_codec_getv(cursor, end, &v);
    if (s != WL_CODEC_OK) {
      return s;
    }
    if (v > UINT32_MAX) {
      return WL_CODEC_ERR_OVERFLOW;
    }
    const int32_t sv = wl_codec_unzz32((uint32_t)v);
    if (f->scalar_size == 1U && (sv < INT8_MIN || sv > INT8_MAX)) {
      return WL_CODEC_ERR_OVERFLOW;
    }
    if (f->scalar_size == 2U && (sv < INT16_MIN || sv > INT16_MAX)) {
      return WL_CODEC_ERR_OVERFLOW;
    }
    write_sint_default(dst, f->scalar_size, sv);
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_INT64:
  case WL_CODEC_KIND_SINT64: {
    uint64_t v = 0U;
    wl_codec_status_t s = wl_codec_getv(cursor, end, &v);
    if (s != WL_CODEC_OK) {
      return s;
    }
    *(int64_t *)dst = wl_codec_unzz64(v);
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_FIXED32:
  case WL_CODEC_KIND_FLOAT32: {
    if ((size_t)(end - *cursor) < 4U) {
      return WL_CODEC_ERR_MALFORMED;
    }
    const uint32_t bits = wl_codec_get_le32(*cursor);
    *cursor += 4U;
    memcpy(dst, &bits, 4U);
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_FIXED64:
  case WL_CODEC_KIND_FLOAT64: {
    if ((size_t)(end - *cursor) < 8U) {
      return WL_CODEC_ERR_MALFORMED;
    }
    const uint64_t bits = wl_codec_get_le64(*cursor);
    *cursor += 8U;
    memcpy(dst, &bits, 8U);
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_BYTES:
  case WL_CODEC_KIND_STRING: {
    uint64_t v = 0U;
    wl_codec_status_t s = wl_codec_getv(cursor, end, &v);
    if (s != WL_CODEC_OK) {
      return s;
    }
    if (f->max_length != 0U && v > (uint64_t)f->max_length) {
      return WL_CODEC_ERR_INVALID_VALUE;
    }
    if (v > (uint64_t)SIZE_MAX) {
      return WL_CODEC_ERR_OVERFLOW;
    }
    const size_t len = (size_t)v;
    if (len > (size_t)(end - *cursor)) {
      return WL_CODEC_ERR_MALFORMED;
    }
    if (f->kind == WL_CODEC_KIND_STRING) {
      s = wl_codec_utf8(*cursor, len);
      if (s != WL_CODEC_OK) {
        return s;
      }
      *(wl_codec_string_t *)dst =
          (wl_codec_string_t){(const char *)*cursor, len};
    } else {
      *(wl_codec_bytes_t *)dst = (wl_codec_bytes_t){*cursor, len};
    }
    *cursor += len;
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_INLINE_BYTES:
  case WL_CODEC_KIND_INLINE_STRING: {
    uint64_t v = 0U;
    wl_codec_status_t s = wl_codec_getv(cursor, end, &v);
    if (s != WL_CODEC_OK) {
      return s;
    }
    if (v > (uint64_t)f->max_length) {
      return WL_CODEC_ERR_INVALID_VALUE;
    }
    const size_t len = (size_t)v;
    if (len > (size_t)(end - *cursor)) {
      return WL_CODEC_ERR_MALFORMED;
    }
    if (f->kind == WL_CODEC_KIND_INLINE_STRING) {
      s = wl_codec_utf8(*cursor, len);
      if (s != WL_CODEC_OK) {
        return s;
      }
    }
    uint8_t *buf = inline_data(dst, f);
    if (len > 0U) {
      memcpy(buf, *cursor, len);
    }
    if (f->kind == WL_CODEC_KIND_INLINE_STRING) {
      buf[len] = '\0';
    }
    *(size_t *)dst = len;
    *cursor += len;
    return WL_CODEC_OK;
  }
  case WL_CODEC_KIND_MESSAGE: {
    if (f->child == NULL || f->child->decode == NULL) {
      return WL_CODEC_ERR_INVALID_VALUE;
    }
    uint64_t v = 0U;
    wl_codec_status_t s = wl_codec_getv(cursor, end, &v);
    if (s != WL_CODEC_OK) {
      return s;
    }
    if (v > (uint64_t)SIZE_MAX) {
      return WL_CODEC_ERR_OVERFLOW;
    }
    const size_t len = (size_t)v;
    if (len > (size_t)(end - *cursor)) {
      return WL_CODEC_ERR_MALFORMED;
    }
    /* Generated tagged children with a field table can use the same fresh
     * destination. Scalar specializations have only a decode callback. */
    s = local && f->child->field_count != 0U
            ? wl_codec_decode_local(f->child, *cursor, len, dst)
            : f->child->decode(*cursor, len, dst);
    if (s != WL_CODEC_OK) {
      return s;
    }
    *cursor += len;
    return WL_CODEC_OK;
  }
  default:
    return WL_CODEC_ERR_INVALID_VALUE;
  }
}

static wl_codec_status_t decode_packed(const wl_codec_field_t *f,
                                       const uint8_t **cursor,
                                       const uint8_t *end, void *dst) {
  uint64_t v = 0U;
  wl_codec_status_t s = wl_codec_getv(cursor, end, &v);
  if (s != WL_CODEC_OK) {
    return s;
  }
  const size_t expected = packed_payload_size(f);
  if (v > (uint64_t)(end - *cursor) || v != (uint64_t)expected) {
    return WL_CODEC_ERR_MALFORMED;
  }
  const uint8_t *p = *cursor;
  uint8_t *out = dst;
  if (f->scalar_size == 4U) {
    for (uint32_t i = 0U; i < f->max_length; ++i) {
      const uint32_t bits = wl_codec_get_le32(p + (size_t)i * 4U);
      memcpy(out + (size_t)i * 4U, &bits, 4U);
    }
  } else {
    for (uint32_t i = 0U; i < f->max_length; ++i) {
      const uint64_t bits = wl_codec_get_le64(p + (size_t)i * 8U);
      memcpy(out + (size_t)i * 8U, &bits, 8U);
    }
  }
  *cursor += expected;
  return WL_CODEC_OK;
}

WL_HOT_ATTR wl_codec_status_t wl_codec_measure(const wl_codec_desc_t *desc,
                                               const void *msg,
                                               size_t *out_size) {
  if (desc == NULL || msg == NULL || out_size == NULL) {
    return WL_CODEC_ERR_INVALID_VALUE;
  }
  const uint8_t *base = msg;
  size_t total = 0U;
  for (size_t i = 0U; i < desc->field_count; ++i) {
    const wl_codec_field_t *f = &desc->fields[i];
    if (f->cardinality == WL_CODEC_CARD_REPEATED) {
      const size_t count = *(const size_t *)(base + f->count_offset);
      const uint8_t *items = *(const uint8_t *const *)(base + f->offset);
      if (count > 0U && items == NULL) {
        return WL_CODEC_ERR_INVALID_VALUE;
      }
      for (size_t elem = 0U; elem < count; ++elem) {
        size_t value_bytes = 0U;
        wl_codec_status_t status =
            measure_value(f, items + elem * f->element_stride, &value_bytes);
        if (status != WL_CODEC_OK) {
          return status;
        }
        status = add_size(&total, f->key_len);
        if (status != WL_CODEC_OK) {
          return status;
        }
        status = add_size(&total, value_bytes);
        if (status != WL_CODEC_OK) {
          return status;
        }
      }
      continue;
    }
    if (!is_required_card(f->cardinality) &&
        !*(const bool *)(base + f->presence_offset)) {
      continue;
    }
    size_t value_bytes = 0U;
    if (is_packed_card(f->cardinality)) {
      const size_t payload = packed_payload_size(f);
      value_bytes = wl_codec_vsize((uint64_t)payload) + payload;
    } else {
      wl_codec_status_t status =
          measure_value(f, base + f->offset, &value_bytes);
      if (status != WL_CODEC_OK) {
        return status;
      }
    }
    wl_codec_status_t status = add_size(&total, f->key_len);
    if (status != WL_CODEC_OK) {
      return status;
    }
    status = add_size(&total, value_bytes);
    if (status != WL_CODEC_OK) {
      return status;
    }
  }
  *out_size = total;
  return WL_CODEC_OK;
}

static bool try_encode_fast(const wl_codec_desc_t *desc, const void *msg,
                            uint8_t *out, size_t out_capacity,
                            size_t *out_length) {
  if (desc == NULL || msg == NULL || out == NULL || out_length == NULL) {
    return false;
  }
  const uint8_t *base = msg;
  uint8_t *cursor = out;
  size_t rem = out_capacity;
  for (size_t i = 0U; i < desc->field_count; ++i) {
    const wl_codec_field_t *f = &desc->fields[i];
    if (f->cardinality == WL_CODEC_CARD_REPEATED) {
      return false;
    }
    if (!is_required_card(f->cardinality) &&
        !*(const bool *)(base + f->presence_offset)) {
      continue;
    }
    if (rem < f->key_len) {
      return false;
    }
    memcpy(cursor, f->key, f->key_len);
    cursor += f->key_len;
    rem -= f->key_len;
    const void *src = base + f->offset;
    if (is_packed_card(f->cardinality)) {
      const size_t payload = packed_payload_size(f);
      const size_t lp = wl_codec_vsize((uint64_t)payload);
      if (payload > rem || lp > rem - payload) {
        return false;
      }
      cursor = emit_packed(f, src, cursor);
      rem -= lp + payload;
      continue;
    }
    switch (f->kind) {
    case WL_CODEC_KIND_BOOL:
      if (rem < 1U) {
        return false;
      }
      *cursor++ = (*(const bool *)src) ? 1U : 0U;
      --rem;
      break;
    case WL_CODEC_KIND_UINT32:
    case WL_CODEC_KIND_UINT64: {
      const uint64_t v = read_unsigned(src, f->scalar_size);
      const size_t nb = wl_codec_vsize(v);
      if (rem < nb) {
        return false;
      }
      cursor = wl_codec_putv(cursor, v);
      rem -= nb;
      break;
    }
    case WL_CODEC_KIND_INT32:
    case WL_CODEC_KIND_SINT32:
    case WL_CODEC_KIND_ENUM: {
      const uint64_t v =
          (uint64_t)wl_codec_zz32((int32_t)read_signed(src, f->scalar_size));
      const size_t nb = wl_codec_vsize(v);
      if (rem < nb) {
        return false;
      }
      cursor = wl_codec_putv(cursor, v);
      rem -= nb;
      break;
    }
    case WL_CODEC_KIND_INT64:
    case WL_CODEC_KIND_SINT64: {
      const uint64_t v = wl_codec_zz64(read_signed(src, f->scalar_size));
      const size_t nb = wl_codec_vsize(v);
      if (rem < nb) {
        return false;
      }
      cursor = wl_codec_putv(cursor, v);
      rem -= nb;
      break;
    }
    case WL_CODEC_KIND_FIXED32:
    case WL_CODEC_KIND_FLOAT32: {
      if (rem < 4U) {
        return false;
      }
      uint32_t bits = 0U;
      memcpy(&bits, src, 4U);
      wl_codec_put_le32(cursor, bits);
      cursor += 4U;
      rem -= 4U;
      break;
    }
    case WL_CODEC_KIND_FIXED64:
    case WL_CODEC_KIND_FLOAT64: {
      if (rem < 8U) {
        return false;
      }
      uint64_t bits = 0U;
      memcpy(&bits, src, 8U);
      wl_codec_put_le64(cursor, bits);
      cursor += 8U;
      rem -= 8U;
      break;
    }
    case WL_CODEC_KIND_BYTES: {
      const wl_codec_bytes_t *b = src;
      if ((b->length > 0U && b->data == NULL) ||
          (f->max_length != 0U && b->length > (size_t)f->max_length)) {
        return false;
      }
      const size_t lp = wl_codec_vsize((uint64_t)b->length);
      if (b->length > rem || lp > rem - b->length) {
        return false;
      }
      cursor = wl_codec_putv(cursor, (uint64_t)b->length);
      if (b->length > 0U) {
        memcpy(cursor, b->data, b->length);
        cursor += b->length;
      }
      rem -= lp + b->length;
      break;
    }
    case WL_CODEC_KIND_INLINE_BYTES: {
      const size_t len = *(const size_t *)src;
      if (len > (size_t)f->max_length) {
        return false;
      }
      const size_t lp = wl_codec_vsize((uint64_t)len);
      if (len > rem || lp > rem - len) {
        return false;
      }
      cursor = wl_codec_putv(cursor, (uint64_t)len);
      if (len > 0U) {
        memcpy(cursor, inline_cdata(src, f), len);
        cursor += len;
      }
      rem -= lp + len;
      break;
    }
    default:
      return false;
    }
  }
  *out_length = (size_t)(cursor - out);
  return true;
}

WL_HOT_ATTR wl_codec_status_t wl_codec_encode(const wl_codec_desc_t *desc,
                                              const void *msg, uint8_t *out,
                                              size_t out_capacity,
                                              size_t *out_length) {
  if (try_encode_fast(desc, msg, out, out_capacity, out_length)) {
    return WL_CODEC_OK;
  }
  size_t needed = 0U;
  wl_codec_status_t status = wl_codec_measure(desc, msg, &needed);
  if (status != WL_CODEC_OK) {
    return status;
  }
  if (out_length == NULL || (needed != 0U && out == NULL)) {
    return WL_CODEC_ERR_INVALID_VALUE;
  }
  if (out_capacity < needed) {
    return WL_CODEC_ERR_CAPACITY;
  }
  const uint8_t *base = msg;
  uint8_t *cursor = out;
  for (size_t i = 0U; i < desc->field_count; ++i) {
    const wl_codec_field_t *f = &desc->fields[i];
    if (f->cardinality == WL_CODEC_CARD_REPEATED) {
      const size_t count = *(const size_t *)(base + f->count_offset);
      const uint8_t *items = *(const uint8_t *const *)(base + f->offset);
      for (size_t elem = 0U; elem < count; ++elem) {
        memcpy(cursor, f->key, f->key_len);
        cursor += f->key_len;
        cursor = emit_value(f, items + elem * f->element_stride, cursor,
                            &status);
        if (status != WL_CODEC_OK) {
          return status;
        }
      }
      continue;
    }
    if (!is_required_card(f->cardinality) &&
        !*(const bool *)(base + f->presence_offset)) {
      continue;
    }
    memcpy(cursor, f->key, f->key_len);
    cursor += f->key_len;
    if (is_packed_card(f->cardinality)) {
      cursor = emit_packed(f, base + f->offset, cursor);
    } else {
      cursor = emit_value(f, base + f->offset, cursor, &status);
      if (status != WL_CODEC_OK) {
        return status;
      }
    }
  }
  *out_length = (size_t)(cursor - out);
  return WL_CODEC_OK;
}

static void copy_decoded(const wl_codec_desc_t *desc, void *out,
                         const void *scratch) {
  if (desc->struct_size <= 64U || desc->field_count == 0U) {
    memcpy(out, scratch, desc->struct_size);
    return;
  }
  uint8_t *out_base = out;
  const uint8_t *src_base = scratch;
  for (size_t i = 0U; i < desc->field_count; ++i) {
    const wl_codec_field_t *f = &desc->fields[i];
    if (f->cardinality == WL_CODEC_CARD_REPEATED) {
      *(size_t *)(out_base + f->count_offset) =
          *(const size_t *)(src_base + f->count_offset);
      continue;
    }
    if (!is_required_card(f->cardinality)) {
      *(bool *)(out_base + f->presence_offset) =
          *(const bool *)(src_base + f->presence_offset);
    }
    void *dst = out_base + f->offset;
    const void *src = src_base + f->offset;
    if (is_packed_card(f->cardinality)) {
      memcpy(dst, src, packed_payload_size(f));
      continue;
    }
    switch (f->kind) {
    case WL_CODEC_KIND_INLINE_BYTES: {
      const size_t len = *(const size_t *)src;
      *(size_t *)dst = len;
      if (len > 0U) {
        memcpy(inline_data(dst, f), inline_cdata(src, f), len);
      }
      break;
    }
    case WL_CODEC_KIND_INLINE_STRING: {
      const size_t len = *(const size_t *)src;
      *(size_t *)dst = len;
      memcpy(inline_data(dst, f), inline_cdata(src, f), len + 1U);
      break;
    }
    case WL_CODEC_KIND_BOOL:
      *(bool *)dst = *(const bool *)src;
      break;
    case WL_CODEC_KIND_UINT32:
    case WL_CODEC_KIND_UINT64:
    case WL_CODEC_KIND_INT32:
    case WL_CODEC_KIND_SINT32:
    case WL_CODEC_KIND_INT64:
    case WL_CODEC_KIND_SINT64:
    case WL_CODEC_KIND_ENUM:
      memcpy(dst, src, f->scalar_size);
      break;
    case WL_CODEC_KIND_FIXED32:
    case WL_CODEC_KIND_FLOAT32:
      memcpy(dst, src, 4U);
      break;
    case WL_CODEC_KIND_FIXED64:
    case WL_CODEC_KIND_FLOAT64:
      memcpy(dst, src, 8U);
      break;
    case WL_CODEC_KIND_BYTES:
      *(wl_codec_bytes_t *)dst = *(const wl_codec_bytes_t *)src;
      break;
    case WL_CODEC_KIND_STRING:
      *(wl_codec_string_t *)dst = *(const wl_codec_string_t *)src;
      break;
    case WL_CODEC_KIND_MESSAGE:
      if (f->child != NULL) {
        copy_decoded(f->child, dst, src);
      }
      break;
    default:
      break;
    }
  }
}

static int try_decode_inorder(const wl_codec_desc_t *desc,
                              const uint8_t *input, size_t input_length,
                              void *message,
                              wl_codec_status_t *out_status) {
  if (input_length == 0U || desc->has_repeated != 0U ||
      desc->presence_bytes != 0U || desc->field_count == 0U) {
    return 0;
  }
  const uint8_t *cursor = input;
  const uint8_t *const end = input + input_length;
  uint8_t *const base = message;
  for (size_t i = 0U; i < desc->field_count; ++i) {
    const wl_codec_field_t *f = &desc->fields[i];
    if (f->cardinality != WL_CODEC_CARD_REQUIRED || f->key_len != 1U ||
        cursor >= end || *cursor != f->key[0]) {
      return 0;
    }
    ++cursor;
    void *dst = base + f->offset;
    switch (f->kind) {
    case WL_CODEC_KIND_BOOL: {
      uint64_t v = 0U;
      if (wl_codec_getv(&cursor, end, &v) != WL_CODEC_OK || v > 1U) {
        return 0;
      }
      *(bool *)dst = (v != 0U);
      break;
    }
    case WL_CODEC_KIND_UINT32: {
      uint64_t v = 0U;
      if (wl_codec_getv(&cursor, end, &v) != WL_CODEC_OK) {
        return 0;
      }
      const uint64_t limit = (f->scalar_size == 1U)   ? UINT8_MAX
                             : (f->scalar_size == 2U) ? UINT16_MAX
                                                      : UINT32_MAX;
      if (v > limit) {
        return 0;
      }
      write_uint_default(dst, f->scalar_size, v);
      break;
    }
    case WL_CODEC_KIND_UINT64: {
      uint64_t v = 0U;
      if (wl_codec_getv(&cursor, end, &v) != WL_CODEC_OK) {
        return 0;
      }
      *(uint64_t *)dst = v;
      break;
    }
    case WL_CODEC_KIND_INT32:
    case WL_CODEC_KIND_SINT32:
    case WL_CODEC_KIND_ENUM: {
      uint64_t v = 0U;
      if (wl_codec_getv(&cursor, end, &v) != WL_CODEC_OK || v > UINT32_MAX) {
        return 0;
      }
      const int32_t sv = wl_codec_unzz32((uint32_t)v);
      if ((f->scalar_size == 1U && (sv < INT8_MIN || sv > INT8_MAX)) ||
          (f->scalar_size == 2U && (sv < INT16_MIN || sv > INT16_MAX))) {
        return 0;
      }
      write_sint_default(dst, f->scalar_size, sv);
      break;
    }
    case WL_CODEC_KIND_INT64:
    case WL_CODEC_KIND_SINT64: {
      uint64_t v = 0U;
      if (wl_codec_getv(&cursor, end, &v) != WL_CODEC_OK) {
        return 0;
      }
      *(int64_t *)dst = wl_codec_unzz64(v);
      break;
    }
    case WL_CODEC_KIND_FIXED32:
    case WL_CODEC_KIND_FLOAT32: {
      if ((size_t)(end - cursor) < 4U) {
        return 0;
      }
      const uint32_t bits = wl_codec_get_le32(cursor);
      cursor += 4U;
      memcpy(dst, &bits, 4U);
      break;
    }
    case WL_CODEC_KIND_FIXED64:
    case WL_CODEC_KIND_FLOAT64: {
      if ((size_t)(end - cursor) < 8U) {
        return 0;
      }
      const uint64_t bits = wl_codec_get_le64(cursor);
      cursor += 8U;
      memcpy(dst, &bits, 8U);
      break;
    }
    case WL_CODEC_KIND_BYTES: {
      uint64_t v = 0U;
      if (wl_codec_getv(&cursor, end, &v) != WL_CODEC_OK ||
          (f->max_length != 0U && v > (uint64_t)f->max_length) ||
          v > (uint64_t)(end - cursor)) {
        return 0;
      }
      const size_t len = (size_t)v;
      *(wl_codec_bytes_t *)dst = (wl_codec_bytes_t){cursor, len};
      cursor += len;
      break;
    }
    case WL_CODEC_KIND_INLINE_BYTES: {
      uint64_t v = 0U;
      if (wl_codec_getv(&cursor, end, &v) != WL_CODEC_OK ||
          v > (uint64_t)f->max_length || v > (uint64_t)(end - cursor)) {
        return 0;
      }
      const size_t len = (size_t)v;
      if (len > 0U) {
        memcpy(inline_data(dst, f), cursor, len);
      }
      *(size_t *)dst = len;
      cursor += len;
      break;
    }
    default:
      return 0;
    }
  }
  if (cursor != end) {
    return 0;
  }
  *out_status = WL_CODEC_OK;
  return 1;
}

static wl_codec_status_t decode_fields(const wl_codec_desc_t *desc,
                                       const uint8_t *input, size_t input_length,
                                       void *dst, bool local) {
  wl_codec_status_t fast_status = WL_CODEC_OK;
  if (try_decode_inorder(desc, input, input_length, dst,
                         &fast_status)) {
    return fast_status;
  }
  uint64_t seen[WL_CODEC_MAX_FIELDS / 64U] = {0};
  wl_codec_clear(desc, dst);
  uint8_t *base = dst;
  const uint8_t *cursor = input;
  const uint8_t *end = input_length != 0U ? input + input_length : input;
  const wl_codec_field_t *next = desc->fields;
  const wl_codec_field_t *const fields_end =
      desc->field_count == 0U ? next : next + desc->field_count;
  while (cursor < end) {
    uint64_t key = 0U;
    wl_codec_status_t status = wl_codec_getv(&cursor, end, &key);
    if (status != WL_CODEC_OK) {
      return status;
    }
    const uint64_t raw_number = key >> 3U;
    const uint8_t wire = (uint8_t)(key & 7U);
    if (raw_number == 0U || raw_number > UINT16_MAX ||
        (wire != 0U && wire != 1U && wire != 2U && wire != 5U)) {
      return WL_CODEC_ERR_MALFORMED;
    }
    const uint16_t number = (uint16_t)raw_number;
    const wl_codec_field_t *f =
        (next != fields_end && next->number == number)
            ? next
            : wl_codec_find_field(desc, number);
    if (f == NULL) {
      status = wl_codec_skip(&cursor, end, wire);
      if (status != WL_CODEC_OK) {
        return status;
      }
      continue;
    }
    if (wire != f->wire_type) {
      return WL_CODEC_ERR_WIRE_TYPE;
    }
    next = f->cardinality == WL_CODEC_CARD_REPEATED ? f : f + 1;
    if (f->cardinality == WL_CODEC_CARD_REPEATED) {
      size_t *count = (size_t *)(base + f->count_offset);
      const size_t capacity = *(const size_t *)(base + f->capacity_offset);
      uint8_t *storage = *(uint8_t **)(base + f->offset);
      if (storage == NULL && capacity > 0U) {
        return WL_CODEC_ERR_INVALID_VALUE;
      }
      if (*count >= capacity) {
        return WL_CODEC_ERR_CAPACITY;
      }
      if (storage == NULL) {
        return WL_CODEC_ERR_INVALID_VALUE;
      }
      status = decode_value(f, &cursor, end,
                            storage + (*count) * f->element_stride, false);
      if (status != WL_CODEC_OK) {
        return status;
      }
      ++(*count);
    } else {
      const size_t index = (size_t)(f - desc->fields);
      const uint64_t bit = UINT64_C(1) << (index & 63U);
      if ((seen[index >> 6U] & bit) != 0U) {
        return WL_CODEC_ERR_DUPLICATE_FIELD;
      }
      if (is_packed_card(f->cardinality)) {
        status = decode_packed(f, &cursor, end, base + f->offset);
      } else {
        status = decode_value(f, &cursor, end, base + f->offset, local);
      }
      if (status != WL_CODEC_OK) {
        return status;
      }
      seen[index >> 6U] |= bit;
      if (!is_required_card(f->cardinality))
        *(bool *)(base + f->presence_offset) = true;
    }
  }
  for (size_t i = 0U; i < desc->field_count; ++i) {
    const wl_codec_field_t *f = &desc->fields[i];
    if (is_required_card(f->cardinality) &&
        (seen[i >> 6U] & (UINT64_C(1) << (i & 63U))) == 0U) {
      return WL_CODEC_ERR_MISSING_REQUIRED_FIELD;
    }
  }
  return WL_CODEC_OK;
}

WL_HOT_ATTR wl_codec_status_t wl_codec_decode(const wl_codec_desc_t *desc,
                                              const uint8_t *input,
                                              size_t input_length, void *out,
                                              void *scratch) {
  if (desc == NULL || out == NULL || scratch == NULL ||
      desc->field_count > WL_CODEC_MAX_FIELDS ||
      (input_length != 0U && input == NULL)) {
    return WL_CODEC_ERR_INVALID_VALUE;
  }
  if (desc->has_repeated != 0U) memcpy(scratch, out, desc->struct_size);
  wl_codec_status_t status = decode_fields(desc, input, input_length, scratch, false);
  if (status == WL_CODEC_OK) copy_decoded(desc, out, scratch);
  return status;
}

WL_HOT_ATTR wl_codec_status_t wl_codec_decode_local(const wl_codec_desc_t *desc,
                                                    const uint8_t *input,
                                                    size_t input_length, void *dst) {
  if (desc == NULL || dst == NULL || desc->has_repeated != 0U ||
      desc->field_count > WL_CODEC_MAX_FIELDS || (input_length != 0U && input == NULL))
    return WL_CODEC_ERR_INVALID_VALUE;
  return decode_fields(desc, input, input_length, dst, true);
}

WL_HOT_ATTR wl_codec_status_t
wl_codec_positional_measure(const wl_codec_desc_t *desc, const void *msg,
                            size_t *out_size) {
  if (desc == NULL || msg == NULL || out_size == NULL) {
    return WL_CODEC_ERR_INVALID_VALUE;
  }
  const uint8_t *base = msg;
  size_t total = desc->presence_bytes;
  for (size_t i = 0U; i < desc->field_count; ++i) {
    const wl_codec_field_t *f = &desc->fields[i];
    if (!is_required_card(f->cardinality) &&
        !*(const bool *)(base + f->presence_offset)) {
      continue;
    }
    size_t value_bytes = 0U;
    if (is_packed_card(f->cardinality)) {
      const size_t payload = packed_payload_size(f);
      value_bytes = wl_codec_vsize((uint64_t)payload) + payload;
    } else {
      wl_codec_status_t status =
          measure_value(f, base + f->offset, &value_bytes);
      if (status != WL_CODEC_OK) {
        return status;
      }
    }
    wl_codec_status_t status = add_size(&total, value_bytes);
    if (status != WL_CODEC_OK) {
      return status;
    }
  }
  *out_size = total;
  return WL_CODEC_OK;
}

WL_HOT_ATTR wl_codec_status_t
wl_codec_positional_encode(const wl_codec_desc_t *desc, const void *msg,
                           uint8_t *out, size_t out_capacity,
                           size_t *out_length) {
  size_t needed = 0U;
  wl_codec_status_t status = wl_codec_positional_measure(desc, msg, &needed);
  if (status != WL_CODEC_OK) {
    return status;
  }
  if (out_length == NULL || (needed != 0U && out == NULL)) {
    return WL_CODEC_ERR_INVALID_VALUE;
  }
  if (out_capacity < needed) {
    return WL_CODEC_ERR_CAPACITY;
  }
  const uint8_t *base = msg;
  uint8_t *cursor = out;
  if (desc->presence_bytes > 0U) {
    memset(cursor, 0, desc->presence_bytes);
    size_t opt_bit = 0U;
    for (size_t i = 0U; i < desc->field_count; ++i) {
      const wl_codec_field_t *f = &desc->fields[i];
      if (!is_required_card(f->cardinality)) {
        if (*(const bool *)(base + f->presence_offset)) {
          cursor[opt_bit >> 3U] |= (uint8_t)(1U << (opt_bit & 7U));
        }
        ++opt_bit;
      }
    }
    cursor += desc->presence_bytes;
  }
  for (size_t i = 0U; i < desc->field_count; ++i) {
    const wl_codec_field_t *f = &desc->fields[i];
    if (!is_required_card(f->cardinality) &&
        !*(const bool *)(base + f->presence_offset)) {
      continue;
    }
    if (is_packed_card(f->cardinality)) {
      cursor = emit_packed(f, base + f->offset, cursor);
    } else {
      cursor = emit_value(f, base + f->offset, cursor, &status);
      if (status != WL_CODEC_OK) {
        return status;
      }
    }
  }
  *out_length = (size_t)(cursor - out);
  return WL_CODEC_OK;
}

WL_HOT_ATTR wl_codec_status_t
wl_codec_positional_decode(const wl_codec_desc_t *desc, const uint8_t *input,
                           size_t input_length, void *out, void *scratch) {
  if (desc == NULL || out == NULL || scratch == NULL ||
      desc->field_count > WL_CODEC_MAX_FIELDS ||
      (input_length != 0U && input == NULL)) {
    return WL_CODEC_ERR_INVALID_VALUE;
  }
  wl_codec_clear(desc, scratch);
  const uint8_t *cursor = input;
  const uint8_t *end = input_length != 0U ? input + input_length : input;
  if (input_length < desc->presence_bytes) {
    return WL_CODEC_ERR_MALFORMED;
  }
  const uint8_t *bitmap = cursor;
  if (desc->presence_bytes > 0U) {
    cursor += desc->presence_bytes;
    size_t opt_count = 0U;
    for (size_t i = 0U; i < desc->field_count; ++i) {
      if (!is_required_card(desc->fields[i].cardinality)) {
        ++opt_count;
      }
    }
    const size_t tail = opt_count & 7U;
    if (tail != 0U &&
        (bitmap[desc->presence_bytes - 1U] & (uint8_t)~((1U << tail) - 1U)) !=
            0U) {
      return WL_CODEC_ERR_MALFORMED;
    }
  }
  uint8_t *base = scratch;
  size_t opt_bit = 0U;
  for (size_t i = 0U; i < desc->field_count; ++i) {
    const wl_codec_field_t *f = &desc->fields[i];
    if (!is_required_card(f->cardinality)) {
      const size_t bit = opt_bit++;
      if (((bitmap[bit >> 3U] >> (bit & 7U)) & 1U) == 0U) {
        continue;
      }
    }
    wl_codec_status_t status = is_packed_card(f->cardinality)
                                   ? decode_packed(f, &cursor, end,
                                                   base + f->offset)
                                   : decode_value(f, &cursor, end,
                                                  base + f->offset, false);
    if (status != WL_CODEC_OK) {
      return status;
    }
    if (!is_required_card(f->cardinality))
      *(bool *)(base + f->presence_offset) = true;
  }
  if (cursor != end) {
    return WL_CODEC_ERR_MALFORMED;
  }
  copy_decoded(desc, out, scratch);
  return WL_CODEC_OK;
}
