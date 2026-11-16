/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)

typedef struct {
  uint32_t sample;
  uint32_t channels[2];
  bool has_extra;
  uint32_t extra;
  bool has_optional_channels;
  uint32_t optional_channels[2];
} message_t;

/* Deliberately unusable presence offsets prove required fields never touch
 * presence storage; UINT16_MAX is not a runtime sentinel. */
static const wl_codec_field_t fields[] = {
    {.number = 7U, .kind = WL_CODEC_KIND_UINT32,
     .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 0U, .scalar_size = 4U,
     .key_len = 1U, .key = {0x38U}, .presence_offset = UINT16_MAX,
     .offset = offsetof(message_t, sample)},
    {.number = 8U, .kind = WL_CODEC_KIND_FIXED32,
     .cardinality = WL_CODEC_CARD_REQUIRED_PACKED, .wire_type = 2U, .scalar_size = 4U,
     .key_len = 1U, .key = {0x42U}, .presence_offset = UINT16_MAX,
     .offset = offsetof(message_t, channels), .max_length = 2U},
    {.number = 9U, .kind = WL_CODEC_KIND_UINT32,
     .cardinality = WL_CODEC_CARD_OPTIONAL, .wire_type = 0U, .scalar_size = 4U,
     .key_len = 1U, .key = {0x48U}, .presence_offset = offsetof(message_t, has_extra),
     .offset = offsetof(message_t, extra), .has_default = 1U, .default_bits = 5U},
    {.number = 10U, .kind = WL_CODEC_KIND_FIXED32,
     .cardinality = WL_CODEC_CARD_PACKED, .wire_type = 2U, .scalar_size = 4U,
     .key_len = 1U, .key = {0x52U},
     .presence_offset = offsetof(message_t, has_optional_channels),
     .offset = offsetof(message_t, optional_channels), .max_length = 2U},
};
static const wl_codec_desc_t descriptor = {
    .fields = fields, .field_count = 4U, .struct_size = sizeof(message_t),
    .presence_bytes = 1U, .min_number = 7U, .max_number = 10U,
};

typedef wl_codec_status_t (*encode_fn)(const wl_codec_desc_t *, const void *,
                                       uint8_t *, size_t, size_t *);
typedef wl_codec_status_t (*decode_fn)(const wl_codec_desc_t *, const uint8_t *,
                                       size_t, void *, void *);
typedef wl_codec_status_t (*measure_fn)(const wl_codec_desc_t *, const void *, size_t *);

static void test_required_and_optional(int positional) {
  encode_fn encode = positional ? wl_codec_positional_encode : wl_codec_encode;
  decode_fn decode = positional ? wl_codec_positional_decode : wl_codec_decode;
  measure_fn measure = positional ? wl_codec_positional_measure : wl_codec_measure;
  uint8_t wire[64];
  for (unsigned optionals = 0U; optionals < 4U; ++optionals) {
    message_t input = {.sample = 0U, .channels = {1U, UINT32_MAX},
        .has_extra = (optionals & 1U) != 0U, .extra = 1234U,
        .has_optional_channels = (optionals & 2U) != 0U,
        .optional_channels = {2U, 3U}};
    message_t output, scratch;
    memset(&output, 0xA5, sizeof(output));
    size_t measured = 0U, length = 0U;
    CHECK(measure(&descriptor, &input, &measured) == WL_CODEC_OK);
    CHECK(encode(&descriptor, &input, wire, sizeof(wire), &length) == WL_CODEC_OK);
    CHECK(length == measured);
    CHECK(decode(&descriptor, wire, length, &output, &scratch) == WL_CODEC_OK);
    CHECK(output.sample == 0U && output.channels[0] == 1U &&
          output.channels[1] == UINT32_MAX);
    CHECK(output.has_extra == input.has_extra);
    CHECK(output.extra == (input.has_extra ? 1234U : 5U));
    CHECK(output.has_optional_channels == input.has_optional_channels);
    CHECK(output.optional_channels[0] == (input.has_optional_channels ? 2U : 0U));
    CHECK(output.optional_channels[1] == (input.has_optional_channels ? 3U : 0U));
    unsigned char saved[sizeof(output)];
    memcpy(saved, &output, sizeof(saved));
    CHECK(decode(&descriptor, wire, length - 1U, &output, &scratch) != WL_CODEC_OK);
    CHECK(memcmp(saved, &output, sizeof(saved)) == 0);
  }
  message_t output, scratch;
  memset(&output, 0xA5, sizeof(output));
  unsigned char saved[sizeof(output)];
  memcpy(saved, &output, sizeof(saved));
  if (!positional) {
    const uint8_t missing[] = {0x38U, 42U};
    const uint8_t duplicate_required[] = {0x38U, 1U, 0x38U, 2U};
    const uint8_t duplicate_optional[] = {0x48U, 1U, 0x48U, 2U};
    const uint8_t duplicate_packed[] = {
        0x42U, 8U, 1U, 0U, 0U, 0U, 2U, 0U, 0U, 0U,
        0x42U, 8U, 1U, 0U, 0U, 0U, 2U, 0U, 0U, 0U};
    CHECK(decode(&descriptor, missing, sizeof(missing), &output, &scratch) ==
          WL_CODEC_ERR_MISSING_REQUIRED_FIELD);
    CHECK(decode(&descriptor, duplicate_required, sizeof(duplicate_required),
                 &output, &scratch) == WL_CODEC_ERR_DUPLICATE_FIELD);
    CHECK(decode(&descriptor, duplicate_optional, sizeof(duplicate_optional),
                 &output, &scratch) == WL_CODEC_ERR_DUPLICATE_FIELD);
    CHECK(decode(&descriptor, duplicate_packed, sizeof(duplicate_packed),
                 &output, &scratch) == WL_CODEC_ERR_DUPLICATE_FIELD);
    CHECK(decode(&descriptor, NULL, 0U, &output, &scratch) ==
          WL_CODEC_ERR_MISSING_REQUIRED_FIELD);
  } else {
    const uint8_t missing[] = {0U, 42U};
    const uint8_t invalid_bitmap[] = {4U};
    CHECK(decode(&descriptor, missing, sizeof(missing), &output, &scratch) ==
          WL_CODEC_ERR_MALFORMED);
    CHECK(decode(&descriptor, invalid_bitmap, sizeof(invalid_bitmap), &output,
                 &scratch) == WL_CODEC_ERR_MALFORMED);
  }
  CHECK(memcmp(saved, &output, sizeof(saved)) == 0);
  wl_codec_clear(&descriptor, &output);
  CHECK(output.sample == 0U && output.channels[0] == 0U && output.channels[1] == 0U);
  CHECK(!output.has_extra && output.extra == 5U && !output.has_optional_channels);
}

typedef struct { uint32_t value; } child_t;
static const wl_codec_field_t child_field = {
    .number = 1U, .kind = WL_CODEC_KIND_UINT32,
    .cardinality = WL_CODEC_CARD_REQUIRED, .scalar_size = 4U,
    .key_len = 1U, .key = {8U}, .presence_offset = UINT16_MAX,
};
static const wl_codec_desc_t child_descriptor;
static void child_clear(void *out) { wl_codec_clear(&child_descriptor, out); }
static wl_codec_status_t child_measure(const void *out, size_t *size) {
  return wl_codec_measure(&child_descriptor, out, size);
}
static wl_codec_status_t child_encode(const void *input, uint8_t *out,
                                      size_t capacity, size_t *length) {
  return wl_codec_encode(&child_descriptor, input, out, capacity, length);
}
static wl_codec_status_t child_decode(const uint8_t *input, size_t length, void *out) {
  child_t scratch;
  return wl_codec_decode(&child_descriptor, input, length, out, &scratch);
}
static const wl_codec_desc_t child_descriptor = {
    .fields = &child_field, .field_count = 1U, .struct_size = sizeof(child_t),
    .min_number = 1U, .max_number = 1U, .clear = child_clear,
    .measure = child_measure, .encode = child_encode, .decode = child_decode,
};
static const wl_codec_field_t parent_field = {
    .number = 1U, .kind = WL_CODEC_KIND_MESSAGE,
    .cardinality = WL_CODEC_CARD_REQUIRED, .wire_type = 2U,
    .key_len = 1U, .key = {10U}, .presence_offset = UINT16_MAX,
    .child = &child_descriptor,
};
static const wl_codec_desc_t parent_descriptor = {
    .fields = &parent_field, .field_count = 1U, .struct_size = sizeof(child_t),
    .min_number = 1U, .max_number = 1U,
};

static void test_nested(int positional) {
  encode_fn encode = positional ? wl_codec_positional_encode : wl_codec_encode;
  decode_fn decode = positional ? wl_codec_positional_decode : wl_codec_decode;
  uint8_t wire[16];
  child_t input = {37U}, output = {99U}, scratch;
  size_t length = 0U;
  CHECK(encode(&parent_descriptor, &input, wire, sizeof(wire), &length) == WL_CODEC_OK);
  CHECK(decode(&parent_descriptor, wire, length, &output, &scratch) == WL_CODEC_OK);
  CHECK(output.value == 37U);
  const uint8_t missing_child[] = {10U, 0U};
  CHECK(decode(&parent_descriptor, missing_child + (positional ? 1U : 0U),
               positional ? 1U : 2U, &output, &scratch) == WL_CODEC_ERR_MISSING_REQUIRED_FIELD);
  CHECK(output.value == 37U);
}

typedef struct {
  uint32_t required;
  uint32_t *items;
  size_t count;
  size_t capacity;
} repeated_t;
static const wl_codec_field_t repeated_fields[] = {
    {.number = 1U, .kind = WL_CODEC_KIND_UINT32,
     .cardinality = WL_CODEC_CARD_REQUIRED, .scalar_size = 4U,
     .key_len = 1U, .key = {8U}, .presence_offset = UINT16_MAX,
     .offset = offsetof(repeated_t, required)},
    {.number = 2U, .kind = WL_CODEC_KIND_UINT32,
     .cardinality = WL_CODEC_CARD_REPEATED, .scalar_size = 4U,
     .key_len = 1U, .key = {16U}, .presence_offset = UINT16_MAX,
     .offset = offsetof(repeated_t, items), .count_offset = offsetof(repeated_t, count),
     .capacity_offset = offsetof(repeated_t, capacity), .element_stride = sizeof(uint32_t)},
};
static const wl_codec_desc_t repeated_descriptor = {
    .fields = repeated_fields, .field_count = 2U, .struct_size = sizeof(repeated_t),
    .min_number = 1U, .max_number = 2U, .has_repeated = 1U,
};

static void test_repeated(void) {
  const uint8_t wire[] = {16U, 11U, 8U, 7U, 16U, 22U};
  uint32_t items[2] = {0};
  repeated_t output = {.required = 99U, .items = items, .count = 0U, .capacity = 2U};
  repeated_t scratch;
  CHECK(wl_codec_decode(&repeated_descriptor, wire, sizeof(wire), &output, &scratch) ==
        WL_CODEC_OK);
  CHECK(output.required == 7U && output.count == 2U && output.items == items);
  CHECK(items[0] == 11U && items[1] == 22U);
  uint8_t encoded[16];
  size_t length = 0U;
  CHECK(wl_codec_encode(&repeated_descriptor, &output, encoded, sizeof(encoded), &length) ==
        WL_CODEC_OK);
  CHECK(length == sizeof(wire));
  unsigned char saved[sizeof(output)];
  memcpy(saved, &output, sizeof(saved));
  const uint8_t invalid[] = {16U, 33U, 8U, 7U, 8U, 9U};
  CHECK(wl_codec_decode(&repeated_descriptor, invalid, sizeof(invalid), &output, &scratch) ==
        WL_CODEC_ERR_DUPLICATE_FIELD);
  CHECK(memcmp(saved, &output, sizeof(saved)) == 0);
  /* Caller-owned repeated backing is written during decoding, as before;
   * only the output object's pointers/count/scalars commit transactionally. */
  CHECK(items[0] == 33U);
  wl_codec_clear(&repeated_descriptor, &output);
  CHECK(output.required == 0U && output.count == 0U && output.items == items &&
        output.capacity == 2U);
}

static void test_field_limit(int positional) {
  wl_codec_field_t many[WL_CODEC_MAX_FIELDS];
  uint32_t input[WL_CODEC_MAX_FIELDS], output[WL_CODEC_MAX_FIELDS], scratch[WL_CODEC_MAX_FIELDS];
  uint8_t wire[2048];
  memset(many, 0, sizeof(many));
  for (unsigned i = 0U; i < WL_CODEC_MAX_FIELDS; ++i) {
    many[i].number = (uint16_t)(i * 200U + 1U);
    many[i].kind = WL_CODEC_KIND_UINT32;
    many[i].cardinality = WL_CODEC_CARD_REQUIRED;
    many[i].scalar_size = 4U;
    many[i].offset = (uint16_t)(i * sizeof(uint32_t));
    many[i].presence_offset = UINT16_MAX;
    many[i].key_len = (uint8_t)(wl_codec_putv(many[i].key, (uint64_t)many[i].number << 3U) -
                               many[i].key);
    input[i] = i + 1U;
    output[i] = UINT32_MAX;
  }
  wl_codec_desc_t desc = {.fields = many, .field_count = WL_CODEC_MAX_FIELDS,
      .struct_size = sizeof(input), .min_number = 1U,
      .max_number = many[WL_CODEC_MAX_FIELDS - 1U].number, .lookup = WL_CODEC_LOOKUP_BINARY};
  encode_fn encode = positional ? wl_codec_positional_encode : wl_codec_encode;
  decode_fn decode = positional ? wl_codec_positional_decode : wl_codec_decode;
  size_t length = 0U;
  CHECK(encode(&desc, input, wire, sizeof(wire), &length) == WL_CODEC_OK);
  CHECK(decode(&desc, wire, length, output, scratch) == WL_CODEC_OK);
  CHECK(memcmp(input, output, sizeof(input)) == 0);
  if (!positional) {
    const wl_codec_field_t *last = &many[WL_CODEC_MAX_FIELDS - 1U];
    const size_t last_length = last->key_len + wl_codec_vsize(input[WL_CODEC_MAX_FIELDS - 1U]);
    CHECK(decode(&desc, wire, length - last_length, output, scratch) ==
          WL_CODEC_ERR_MISSING_REQUIRED_FIELD);
    CHECK(memcmp(input, output, sizeof(input)) == 0);
    memcpy(wire + length, wire + length - last_length, last_length);
    CHECK(decode(&desc, wire, length + last_length, output, scratch) ==
          WL_CODEC_ERR_DUPLICATE_FIELD);
    CHECK(memcmp(input, output, sizeof(input)) == 0);
  }
  CHECK(decode(&desc, NULL, 0U, output, scratch) ==
        (positional ? WL_CODEC_ERR_MALFORMED : WL_CODEC_ERR_MISSING_REQUIRED_FIELD));
  CHECK(memcmp(input, output, sizeof(input)) == 0);
  desc.field_count = WL_CODEC_MAX_FIELDS + 1U;
  memset(scratch, 0xA5, sizeof(scratch));
  CHECK(decode(&desc, wire, length, output, scratch) == WL_CODEC_ERR_INVALID_VALUE);
  CHECK(memcmp(input, output, sizeof(input)) == 0);
  CHECK(scratch[0] == UINT32_C(0xA5A5A5A5));
}

static void test_helpers(void) {
  const wl_codec_string_t view = WL_STR("hello");
  CHECK(view.length == 5U && strcmp(view.data, "hello") == 0);
  char text[5] = "old";
  size_t length = 3U;
  CHECK(!wl_codec_inline_str_set(&length, text, 4U, "large"));
  CHECK(length == 3U && strcmp(text, "old") == 0);
  CHECK(wl_codec_inline_str_set(&length, text, 4U, "four"));
  CHECK(length == 4U && strcmp(text, "four") == 0);
  CHECK(wl_codec_inline_str_set(&length, text, 4U, text + 1U));
  CHECK(length == 3U && strcmp(text, "our") == 0);
  CHECK(wl_codec_inline_str_set(&length, text, 0U, ""));
  CHECK(length == 0U && text[0] == '\0');
  CHECK(!wl_codec_inline_str_set(&length, text, 4U, NULL));
  uint8_t bytes[3] = {1U, 2U, 3U};
  const uint8_t source[] = {4U, 5U, 6U, 7U};
  CHECK(!wl_codec_inline_bytes_set(&length, bytes, 3U, source, 4U));
  CHECK(length == 0U && bytes[0] == 1U);
  CHECK(wl_codec_inline_bytes_set(&length, bytes, 3U, source, 3U));
  CHECK(length == 3U && memcmp(bytes, source, 3U) == 0);
  CHECK(wl_codec_inline_bytes_set(&length, bytes, 3U, bytes + 1U, 2U));
  CHECK(length == 2U && bytes[0] == 5U && bytes[1] == 6U);
  CHECK(wl_codec_inline_bytes_set(&length, bytes, 0U, NULL, 0U));
  CHECK(length == 0U);
  CHECK(!wl_codec_inline_bytes_set(&length, bytes, 3U, NULL, 1U));
}

int main(void) {
  test_required_and_optional(0);
  test_required_and_optional(1);
  test_nested(0);
  test_nested(1);
  test_repeated();
  test_field_limit(0);
  test_field_limit(1);
  test_helpers();
  return 0;
}
