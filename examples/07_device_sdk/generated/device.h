#ifndef WIRELINK_GENERATED_DEVICE_H
#define WIRELINK_GENERATED_DEVICE_H

/* Advanced borrowed codec and explicit value/view conversions. */
#include "device_values.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct info_request info_request_t;
typedef struct settings settings_t;
typedef struct info_response info_response_t;
typedef struct configure_request configure_request_t;
typedef struct configure_response configure_response_t;
typedef struct numbers numbers_t;

struct info_request {
  uint8_t _empty;
};

struct settings {
  bool has_label;
  wl_codec_string_t label;
  bool has_enabled;
  bool enabled;
  bool has_mode;
  mode_t mode;
  bool has_token;
  wl_codec_bytes_t token;
  bool has_gains;
  float gains[3];
  bool has_counters;
  uint64_t counters[2];
  bool has_floor;
  int64_t floor;
  bool has_ceiling;
  uint64_t ceiling;
  bool has_class_;
  wl_codec_string_t class_;
  bool has_timeout;
  uint16_t timeout;
};

struct info_response {
  bool has_name;
  wl_codec_string_t name;
  bool has_settings;
  settings_t settings;
};

struct configure_request {
  bool has_settings;
  settings_t settings;
  bool has_opaque;
  wl_codec_bytes_t opaque;
  bool has_backup;
  settings_t backup;
};

struct configure_response {
  bool has_settings;
  settings_t settings;
  bool has_opaque;
  wl_codec_bytes_t opaque;
  bool has_backup;
  settings_t backup;
  bool has_count;
  uint32_t count;
};

struct numbers {
  bool has_i8;
  int8_t i8;
  bool has_u8;
  uint8_t u8;
  bool has_i16;
  int16_t i16;
  bool has_u16;
  uint16_t u16;
  bool has_i32;
  int32_t i32;
  bool has_u32;
  uint32_t u32;
  bool has_i64;
  int64_t i64;
  bool has_u64;
  uint64_t u64;
  bool has_f32;
  uint32_t f32;
  bool has_f64;
  uint64_t f64;
  bool has_single;
  float single;
  bool has_double_;
  double double_;
  bool has_doubles;
  double doubles[2];
  bool has_words;
  uint32_t words[2];
  bool has_offset;
  int64_t offset;
  bool has_minimum;
  int32_t minimum;
  bool has_flag;
  bool flag;
  bool has_text;
  wl_codec_string_t text;
};

void info_request_clear(info_request_t *value);
size_t info_request_encoded_size(const info_request_t *value);
wl_codec_status_t info_request_encode(const info_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t info_request_decode(const uint8_t *input, size_t input_length, info_request_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t info_request_value_from_view(const info_request_t *view, info_request_value_t *out);
wl_codec_status_t info_request_value_to_view(const info_request_value_t *value, info_request_t *out);

void settings_clear(settings_t *value);
size_t settings_encoded_size(const settings_t *value);
wl_codec_status_t settings_encode(const settings_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t settings_decode(const uint8_t *input, size_t input_length, settings_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t settings_value_from_view(const settings_t *view, settings_value_t *out);
wl_codec_status_t settings_value_to_view(const settings_value_t *value, settings_t *out);

void info_response_clear(info_response_t *value);
size_t info_response_encoded_size(const info_response_t *value);
wl_codec_status_t info_response_encode(const info_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t info_response_decode(const uint8_t *input, size_t input_length, info_response_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t info_response_value_from_view(const info_response_t *view, info_response_value_t *out);
wl_codec_status_t info_response_value_to_view(const info_response_value_t *value, info_response_t *out);

void configure_request_clear(configure_request_t *value);
size_t configure_request_encoded_size(const configure_request_t *value);
wl_codec_status_t configure_request_encode(const configure_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t configure_request_decode(const uint8_t *input, size_t input_length, configure_request_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t configure_request_value_from_view(const configure_request_t *view, configure_request_value_t *out);
wl_codec_status_t configure_request_value_to_view(const configure_request_value_t *value, configure_request_t *out);

void configure_response_clear(configure_response_t *value);
size_t configure_response_encoded_size(const configure_response_t *value);
wl_codec_status_t configure_response_encode(const configure_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t configure_response_decode(const uint8_t *input, size_t input_length, configure_response_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t configure_response_value_from_view(const configure_response_t *view, configure_response_value_t *out);
wl_codec_status_t configure_response_value_to_view(const configure_response_value_t *value, configure_response_t *out);

void numbers_clear(numbers_t *value);
size_t numbers_encoded_size(const numbers_t *value);
wl_codec_status_t numbers_encode(const numbers_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t numbers_decode(const uint8_t *input, size_t input_length, numbers_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t numbers_value_from_view(const numbers_t *view, numbers_value_t *out);
wl_codec_status_t numbers_value_to_view(const numbers_value_t *value, numbers_t *out);

#ifdef __cplusplus
}
#endif

#endif
