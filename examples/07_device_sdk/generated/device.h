/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_GENERATED_DEVICE_H
#define WIRELINK_GENERATED_DEVICE_H

#if !defined(__cplusplus)
#include <stdbool.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include <wirelink/codec.h>
#include <float.h>

#if defined(__cplusplus)
static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 && FLT_MIN_EXP == -125, "WLC float32 requires IEEE-754 binary32");
static_assert(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024 && DBL_MIN_EXP == -1021, "WLC float64 requires IEEE-754 binary64");
#else
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 && FLT_MIN_EXP == -125, "WLC float32 requires IEEE-754 binary32");
_Static_assert(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024 && DBL_MIN_EXP == -1021, "WLC float64 requires IEEE-754 binary64");
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t mode_t;
#define MODE_IDLE INT32_C(0)
#define MODE_ACTIVE INT32_C(1)

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
  struct { size_t length; char data[13]; } label;
  bool has_enabled;
  bool enabled;
  bool has_mode;
  mode_t mode;
  bool has_token;
  struct { size_t length; uint8_t data[8]; } token;
  bool has_gains;
  float gains[3];
  bool has_counters;
  uint64_t counters[2];
  bool has_floor;
  int64_t floor;
  bool has_ceiling;
  uint64_t ceiling;
  bool has_class_;
  struct { size_t length; char data[9]; } class_;
  bool has_timeout;
  uint16_t timeout;
};

struct info_response {
  bool has_name;
  struct { size_t length; char data[33]; } name;
  bool has_settings;
  settings_t settings;
};

struct configure_request {
  bool has_settings;
  settings_t settings;
  bool has_opaque;
  struct { size_t length; uint8_t data[4]; } opaque;
  bool has_backup;
  settings_t backup;
};

struct configure_response {
  bool has_settings;
  settings_t settings;
  bool has_opaque;
  struct { size_t length; uint8_t data[4]; } opaque;
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
  struct { size_t length; char data[17]; } text;
};

#define INFO_REQUEST_MESSAGE_ID 2U
#define INFO_REQUEST_CODEC_PROFILE 1U
#define INFO_REQUEST_CODEC_FORMAT "schema-v1"
#define INFO_REQUEST_HAS_MAX_ENCODED_SIZE 1
#define INFO_REQUEST_MAX_ENCODED_SIZE UINT64_C(0)
void info_request_clear(info_request_t *value);
size_t info_request_encoded_size(const info_request_t *value);
wl_codec_status_t info_request_encode(const info_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t info_request_decode(const uint8_t *input, size_t input_length, info_request_t *out);

#define SETTINGS_MESSAGE_ID 3U
#define SETTINGS_CODEC_PROFILE 1U
#define SETTINGS_CODEC_FORMAT "schema-v1"
#define SETTINGS_HAS_MAX_ENCODED_SIZE 1
#define SETTINGS_MAX_ENCODED_SIZE UINT64_C(100)
void settings_clear(settings_t *value);
size_t settings_encoded_size(const settings_t *value);
wl_codec_status_t settings_encode(const settings_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t settings_decode(const uint8_t *input, size_t input_length, settings_t *out);

#define INFO_RESPONSE_MESSAGE_ID 4U
#define INFO_RESPONSE_CODEC_PROFILE 1U
#define INFO_RESPONSE_CODEC_FORMAT "schema-v1"
#define INFO_RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define INFO_RESPONSE_MAX_ENCODED_SIZE UINT64_C(136)
void info_response_clear(info_response_t *value);
size_t info_response_encoded_size(const info_response_t *value);
wl_codec_status_t info_response_encode(const info_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t info_response_decode(const uint8_t *input, size_t input_length, info_response_t *out);

#define CONFIGURE_REQUEST_MESSAGE_ID 5U
#define CONFIGURE_REQUEST_CODEC_PROFILE 1U
#define CONFIGURE_REQUEST_CODEC_FORMAT "schema-v1"
#define CONFIGURE_REQUEST_HAS_MAX_ENCODED_SIZE 1
#define CONFIGURE_REQUEST_MAX_ENCODED_SIZE UINT64_C(210)
void configure_request_clear(configure_request_t *value);
size_t configure_request_encoded_size(const configure_request_t *value);
wl_codec_status_t configure_request_encode(const configure_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t configure_request_decode(const uint8_t *input, size_t input_length, configure_request_t *out);

#define CONFIGURE_RESPONSE_MESSAGE_ID 6U
#define CONFIGURE_RESPONSE_CODEC_PROFILE 1U
#define CONFIGURE_RESPONSE_CODEC_FORMAT "schema-v1"
#define CONFIGURE_RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define CONFIGURE_RESPONSE_MAX_ENCODED_SIZE UINT64_C(216)
void configure_response_clear(configure_response_t *value);
size_t configure_response_encoded_size(const configure_response_t *value);
wl_codec_status_t configure_response_encode(const configure_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t configure_response_decode(const uint8_t *input, size_t input_length, configure_response_t *out);

#define NUMBERS_MESSAGE_ID 7U
#define NUMBERS_CODEC_PROFILE 1U
#define NUMBERS_CODEC_FORMAT "schema-v1"
#define NUMBERS_HAS_MAX_ENCODED_SIZE 1
#define NUMBERS_MAX_ENCODED_SIZE UINT64_C(144)
void numbers_clear(numbers_t *value);
size_t numbers_encoded_size(const numbers_t *value);
wl_codec_status_t numbers_encode(const numbers_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t numbers_decode(const uint8_t *input, size_t input_length, numbers_t *out);

#ifdef __cplusplus
}
#endif

#endif
