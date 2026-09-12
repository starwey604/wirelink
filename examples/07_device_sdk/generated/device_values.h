#ifndef WIRELINK_GENERATED_DEVICE_H_VALUES
#define WIRELINK_GENERATED_DEVICE_H_VALUES

#include <stdbool.h>
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

/* Self-owning business values. Assignment copies all data; no destroy is needed.
 * String lengths are bytes (embedded NUL is allowed); data[length] is a
 * convenience terminator after clear/decode/from_view, not part of the wire.
 * Views borrow their source. Conversion input/output must not overlap.
 * Failed conversions/decodes leave output unchanged. */
#define INFO_REQUEST_HAS_VALUE 1
typedef struct {
  uint8_t _empty;
} info_request_value_t;
#define INFO_REQUEST_VALUE_SIZE (sizeof(info_request_value_t))
void info_request_value_clear(info_request_value_t *value);
size_t info_request_value_encoded_size(const info_request_value_t *value);
wl_codec_status_t info_request_value_encode(const info_request_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t info_request_value_decode(const uint8_t *input, size_t length, info_request_value_t *out);

#define SETTINGS_HAS_VALUE 1
typedef struct {
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
} settings_value_t;
#define SETTINGS_VALUE_SIZE (sizeof(settings_value_t))
void settings_value_clear(settings_value_t *value);
size_t settings_value_encoded_size(const settings_value_t *value);
wl_codec_status_t settings_value_encode(const settings_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t settings_value_decode(const uint8_t *input, size_t length, settings_value_t *out);

#define INFO_RESPONSE_HAS_VALUE 1
typedef struct {
  bool has_name;
  struct { size_t length; char data[33]; } name;
  bool has_settings;
  settings_value_t settings;
} info_response_value_t;
#define INFO_RESPONSE_VALUE_SIZE (sizeof(info_response_value_t))
void info_response_value_clear(info_response_value_t *value);
size_t info_response_value_encoded_size(const info_response_value_t *value);
wl_codec_status_t info_response_value_encode(const info_response_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t info_response_value_decode(const uint8_t *input, size_t length, info_response_value_t *out);

#define CONFIGURE_REQUEST_HAS_VALUE 1
typedef struct {
  bool has_settings;
  settings_value_t settings;
  bool has_opaque;
  struct { size_t length; uint8_t data[4]; } opaque;
  bool has_backup;
  settings_value_t backup;
} configure_request_value_t;
#define CONFIGURE_REQUEST_VALUE_SIZE (sizeof(configure_request_value_t))
void configure_request_value_clear(configure_request_value_t *value);
size_t configure_request_value_encoded_size(const configure_request_value_t *value);
wl_codec_status_t configure_request_value_encode(const configure_request_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t configure_request_value_decode(const uint8_t *input, size_t length, configure_request_value_t *out);

#define CONFIGURE_RESPONSE_HAS_VALUE 1
typedef struct {
  bool has_settings;
  settings_value_t settings;
  bool has_opaque;
  struct { size_t length; uint8_t data[4]; } opaque;
  bool has_backup;
  settings_value_t backup;
  bool has_count;
  uint32_t count;
} configure_response_value_t;
#define CONFIGURE_RESPONSE_VALUE_SIZE (sizeof(configure_response_value_t))
void configure_response_value_clear(configure_response_value_t *value);
size_t configure_response_value_encoded_size(const configure_response_value_t *value);
wl_codec_status_t configure_response_value_encode(const configure_response_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t configure_response_value_decode(const uint8_t *input, size_t length, configure_response_value_t *out);

#define NUMBERS_HAS_VALUE 1
typedef struct {
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
} numbers_value_t;
#define NUMBERS_VALUE_SIZE (sizeof(numbers_value_t))
void numbers_value_clear(numbers_value_t *value);
size_t numbers_value_encoded_size(const numbers_value_t *value);
wl_codec_status_t numbers_value_encode(const numbers_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t numbers_value_decode(const uint8_t *input, size_t length, numbers_value_t *out);

#define INFO_REQUEST_MESSAGE_ID 2U
#define INFO_REQUEST_HAS_MAX_ENCODED_SIZE 1
#define INFO_REQUEST_MAX_ENCODED_SIZE UINT64_C(0)
#define SETTINGS_MESSAGE_ID 3U
#define SETTINGS_HAS_MAX_ENCODED_SIZE 1
#define SETTINGS_MAX_ENCODED_SIZE UINT64_C(100)
#define INFO_RESPONSE_MESSAGE_ID 4U
#define INFO_RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define INFO_RESPONSE_MAX_ENCODED_SIZE UINT64_C(136)
#define CONFIGURE_REQUEST_MESSAGE_ID 5U
#define CONFIGURE_REQUEST_HAS_MAX_ENCODED_SIZE 1
#define CONFIGURE_REQUEST_MAX_ENCODED_SIZE UINT64_C(210)
#define CONFIGURE_RESPONSE_MESSAGE_ID 6U
#define CONFIGURE_RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define CONFIGURE_RESPONSE_MAX_ENCODED_SIZE UINT64_C(216)
#define NUMBERS_MESSAGE_ID 7U
#define NUMBERS_HAS_MAX_ENCODED_SIZE 1
#define NUMBERS_MAX_ENCODED_SIZE UINT64_C(144)
#ifdef __cplusplus
}
#endif

#endif
