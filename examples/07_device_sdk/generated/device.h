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
  bool enabled;
  bool has_mode;
  mode_t mode;
  struct { size_t length; uint8_t data[8]; } token;
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

static inline bool settings_set_label(settings_t *msg, const char *value) {
  if (msg == NULL || !wl_codec_inline_str_set(&msg->label.length, msg->label.data, 12U, value)) return false;
  msg->has_label = true;
  return true;
}

static inline bool settings_set_token(settings_t *msg, const void *value, size_t length) {
  if (msg == NULL || !wl_codec_inline_bytes_set(&msg->token.length, msg->token.data, 8U, value, length)) return false;
  return true;
}

static inline bool settings_set_class_(settings_t *msg, const char *value) {
  if (msg == NULL || !wl_codec_inline_str_set(&msg->class_.length, msg->class_.data, 8U, value)) return false;
  msg->has_class_ = true;
  return true;
}

struct info_response {
  struct { size_t length; char data[33]; } name;
  settings_t settings;
};

static inline bool info_response_set_name(info_response_t *msg, const char *value) {
  if (msg == NULL || !wl_codec_inline_str_set(&msg->name.length, msg->name.data, 32U, value)) return false;
  return true;
}

struct configure_request {
  settings_t settings;
  bool has_opaque;
  struct { size_t length; uint8_t data[4]; } opaque;
  bool has_backup;
  settings_t backup;
};

static inline bool configure_request_set_opaque(configure_request_t *msg, const void *value, size_t length) {
  if (msg == NULL || !wl_codec_inline_bytes_set(&msg->opaque.length, msg->opaque.data, 4U, value, length)) return false;
  msg->has_opaque = true;
  return true;
}

struct configure_response {
  settings_t settings;
  bool has_opaque;
  struct { size_t length; uint8_t data[4]; } opaque;
  bool has_backup;
  settings_t backup;
  uint32_t count;
};

static inline bool configure_response_set_opaque(configure_response_t *msg, const void *value, size_t length) {
  if (msg == NULL || !wl_codec_inline_bytes_set(&msg->opaque.length, msg->opaque.data, 4U, value, length)) return false;
  msg->has_opaque = true;
  return true;
}

struct numbers {
  int8_t i8;
  uint8_t u8;
  int16_t i16;
  uint16_t u16;
  int32_t i32;
  uint32_t u32;
  int64_t i64;
  uint64_t u64;
  uint32_t f32;
  uint64_t f64;
  float single;
  double double_;
  bool has_doubles;
  double doubles[2];
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

static inline bool numbers_set_text(numbers_t *msg, const char *value) {
  if (msg == NULL || !wl_codec_inline_str_set(&msg->text.length, msg->text.data, 16U, value)) return false;
  msg->has_text = true;
  return true;
}

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

#include <wirelink/endpoint.h>
#include <wirelink/version.h>
#if WIRELINK_CODEGEN_CONTRACT != 1
#error "WLC requires generated-code contract 1"
#endif

typedef struct device_node device_node_t;
#define DEVICE_REQUEST_FRAME_MAX 222U
#define DEVICE_REPLY_FRAME_MAX 0U
#define DEVICE_MESSAGE_FRAME_MAX 0U
#define DEVICE_RX_FRAME_MAX 228U
#define DEVICE_TX_FRAME_MAX 222U
#define DEVICE_RX_WIRE_MAX 228U
#define DEVICE_TX_WIRE_MAX 222U
struct device_node {
  wl_endpoint_t core;
  void *user;
  wl_endpoint_error_fn on_error;
  uint8_t requests[WL_ENDPOINT_CALLS][222];
};
wl_err_t device_init(device_node_t *self, wl_packet_port_t *port);
wl_err_t device_step(device_node_t *self, uint32_t now);
/* Stop the RX producer before close, then step until quiescent. */
wl_err_t device_close(device_node_t *self);
int device_quiescent(const device_node_t *self);
wl_err_t device_cancel(device_node_t *self, wl_call_handle_t handle);
typedef void (*device_configure_cb_t)(device_node_t *, wl_rpc_result_t, const configure_response_t *);
typedef void (*device_configure_ex_cb_t)(void *, wl_rpc_result_t, const configure_response_t *);
/* Simplified calls start their timeout on the next step(now). */
wl_err_t device_call_configure(device_node_t *self, const configure_request_t *request, device_configure_cb_t callback);
wl_err_t device_call_configure_ex(device_node_t *self, const configure_request_t *request, uint32_t now, uint32_t timeout_ms, device_configure_ex_cb_t callback, void *user, wl_call_handle_t *out);
typedef void (*device_get_info_cb_t)(device_node_t *, wl_rpc_result_t, const info_response_t *);
typedef void (*device_get_info_ex_cb_t)(void *, wl_rpc_result_t, const info_response_t *);
/* Simplified calls start their timeout on the next step(now). */
wl_err_t device_call_get_info(device_node_t *self, const info_request_t *request, device_get_info_cb_t callback);
wl_err_t device_call_get_info_ex(device_node_t *self, const info_request_t *request, uint32_t now, uint32_t timeout_ms, device_get_info_ex_cb_t callback, void *user, wl_call_handle_t *out);
#ifdef __cplusplus
}
#endif

#endif
