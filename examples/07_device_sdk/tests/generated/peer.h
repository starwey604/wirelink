/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_GENERATED_PEER_H
#define WIRELINK_GENERATED_PEER_H

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

typedef int32_t peer_mode_t;
#define MODE_IDLE INT32_C(0)
#define MODE_ACTIVE INT32_C(1)

typedef struct peer_info_request peer_info_request_t;
typedef struct peer_settings peer_settings_t;
typedef struct peer_info_response peer_info_response_t;
typedef struct peer_configure_request peer_configure_request_t;
typedef struct peer_configure_response peer_configure_response_t;
typedef struct peer_numbers peer_numbers_t;

struct peer_info_request {
  uint8_t _empty;
};

struct peer_settings {
  bool has_label;
  struct { size_t length; char data[13]; } label;
  bool enabled;
  bool has_mode;
  peer_mode_t mode;
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

static inline bool peer_settings_set_label(peer_settings_t *msg, const char *value) {
  if (msg == NULL || !wl_codec_inline_str_set(&msg->label.length, msg->label.data, 12U, value)) return false;
  msg->has_label = true;
  return true;
}

static inline bool peer_settings_set_token(peer_settings_t *msg, const void *value, size_t length) {
  if (msg == NULL || !wl_codec_inline_bytes_set(&msg->token.length, msg->token.data, 8U, value, length)) return false;
  return true;
}

static inline bool peer_settings_set_class_(peer_settings_t *msg, const char *value) {
  if (msg == NULL || !wl_codec_inline_str_set(&msg->class_.length, msg->class_.data, 8U, value)) return false;
  msg->has_class_ = true;
  return true;
}

struct peer_info_response {
  struct { size_t length; char data[33]; } name;
  peer_settings_t settings;
};

static inline bool peer_info_response_set_name(peer_info_response_t *msg, const char *value) {
  if (msg == NULL || !wl_codec_inline_str_set(&msg->name.length, msg->name.data, 32U, value)) return false;
  return true;
}

struct peer_configure_request {
  peer_settings_t settings;
  bool has_opaque;
  struct { size_t length; uint8_t data[4]; } opaque;
  bool has_backup;
  peer_settings_t backup;
};

static inline bool peer_configure_request_set_opaque(peer_configure_request_t *msg, const void *value, size_t length) {
  if (msg == NULL || !wl_codec_inline_bytes_set(&msg->opaque.length, msg->opaque.data, 4U, value, length)) return false;
  msg->has_opaque = true;
  return true;
}

struct peer_configure_response {
  peer_settings_t settings;
  bool has_opaque;
  struct { size_t length; uint8_t data[4]; } opaque;
  bool has_backup;
  peer_settings_t backup;
  uint32_t count;
};

static inline bool peer_configure_response_set_opaque(peer_configure_response_t *msg, const void *value, size_t length) {
  if (msg == NULL || !wl_codec_inline_bytes_set(&msg->opaque.length, msg->opaque.data, 4U, value, length)) return false;
  msg->has_opaque = true;
  return true;
}

struct peer_numbers {
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

static inline bool peer_numbers_set_text(peer_numbers_t *msg, const char *value) {
  if (msg == NULL || !wl_codec_inline_str_set(&msg->text.length, msg->text.data, 16U, value)) return false;
  msg->has_text = true;
  return true;
}

#define PEER_INFO_REQUEST_MESSAGE_ID 2U
#define PEER_INFO_REQUEST_CODEC_PROFILE 1U
#define PEER_INFO_REQUEST_CODEC_FORMAT "schema-v1"
#define PEER_INFO_REQUEST_HAS_MAX_ENCODED_SIZE 1
#define PEER_INFO_REQUEST_MAX_ENCODED_SIZE UINT64_C(0)
void peer_info_request_clear(peer_info_request_t *value);
wl_codec_status_t peer_info_request_decode(const uint8_t *input, size_t input_length, peer_info_request_t *out);

#define PEER_SETTINGS_MESSAGE_ID 3U
#define PEER_SETTINGS_CODEC_PROFILE 1U
#define PEER_SETTINGS_CODEC_FORMAT "schema-v1"
#define PEER_SETTINGS_HAS_MAX_ENCODED_SIZE 1
#define PEER_SETTINGS_MAX_ENCODED_SIZE UINT64_C(100)
void peer_settings_clear(peer_settings_t *value);
size_t peer_settings_encoded_size(const peer_settings_t *value);
wl_codec_status_t peer_settings_encode(const peer_settings_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t peer_settings_decode(const uint8_t *input, size_t input_length, peer_settings_t *out);

#define PEER_INFO_RESPONSE_MESSAGE_ID 4U
#define PEER_INFO_RESPONSE_CODEC_PROFILE 1U
#define PEER_INFO_RESPONSE_CODEC_FORMAT "schema-v1"
#define PEER_INFO_RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define PEER_INFO_RESPONSE_MAX_ENCODED_SIZE UINT64_C(136)
void peer_info_response_clear(peer_info_response_t *value);
size_t peer_info_response_encoded_size(const peer_info_response_t *value);
wl_codec_status_t peer_info_response_encode(const peer_info_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);

#define PEER_CONFIGURE_REQUEST_MESSAGE_ID 5U
#define PEER_CONFIGURE_REQUEST_CODEC_PROFILE 1U
#define PEER_CONFIGURE_REQUEST_CODEC_FORMAT "schema-v1"
#define PEER_CONFIGURE_REQUEST_HAS_MAX_ENCODED_SIZE 1
#define PEER_CONFIGURE_REQUEST_MAX_ENCODED_SIZE UINT64_C(210)
void peer_configure_request_clear(peer_configure_request_t *value);
wl_codec_status_t peer_configure_request_decode(const uint8_t *input, size_t input_length, peer_configure_request_t *out);

#define PEER_CONFIGURE_RESPONSE_MESSAGE_ID 6U
#define PEER_CONFIGURE_RESPONSE_CODEC_PROFILE 1U
#define PEER_CONFIGURE_RESPONSE_CODEC_FORMAT "schema-v1"
#define PEER_CONFIGURE_RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define PEER_CONFIGURE_RESPONSE_MAX_ENCODED_SIZE UINT64_C(216)
void peer_configure_response_clear(peer_configure_response_t *value);
size_t peer_configure_response_encoded_size(const peer_configure_response_t *value);
wl_codec_status_t peer_configure_response_encode(const peer_configure_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);

#define PEER_NUMBERS_MESSAGE_ID 7U
#define PEER_NUMBERS_CODEC_PROFILE 1U
#define PEER_NUMBERS_CODEC_FORMAT "schema-v1"
#define PEER_NUMBERS_HAS_MAX_ENCODED_SIZE 1
#define PEER_NUMBERS_MAX_ENCODED_SIZE UINT64_C(144)
void peer_numbers_clear(peer_numbers_t *value);

#include <wirelink/node.h>
#include <wirelink/version.h>
#if WIRELINK_CODEGEN_CONTRACT != 1
#error "WLC requires generated-code contract 1"
#endif

typedef struct device_peer_node device_peer_node_t;
#define DEVICE_PEER_REQUEST_FRAME_MAX 0U
#define DEVICE_PEER_REPLY_FRAME_MAX 228U
#define DEVICE_PEER_MESSAGE_FRAME_MAX 0U
#define DEVICE_PEER_RX_FRAME_MAX 222U
#define DEVICE_PEER_TX_FRAME_MAX 228U
#define DEVICE_PEER_RX_WIRE_MAX 222U
#define DEVICE_PEER_TX_WIRE_MAX 228U
/* Accept and drain the whole frame, or reject it without consuming bytes. */
typedef wl_err_t (*device_peer_tx_write_fn)(device_peer_node_t *self, const uint8_t *data, size_t length);
typedef struct {
  int32_t (*on_configure)(void *, const peer_configure_request_t *, peer_configure_response_t *);
  int32_t (*on_get_info)(void *, const peer_info_request_t *, peer_info_response_t *);
} device_peer_handlers_t;
struct device_peer_node {
  wl_node_t node;
  device_peer_handlers_t handlers;
  union {
    peer_configure_response_t configure;
    peer_info_response_t get_info;
  } reply;
  uint8_t replies[WL_ENDPOINT_REPLIES][228];
  uint8_t rx[WL_PACKET_RX_SLOTS][DEVICE_PEER_RX_WIRE_MAX + 1U];
  device_peer_tx_write_fn tx_write;
};
/* Explicit instance IDs must be nonzero. Rebind only after close completes. */
wl_err_t device_peer_bind(device_peer_node_t *self, wl_transport_t *transport, const device_peer_handlers_t *handlers, void *user);
wl_err_t device_peer_bind_instance(device_peer_node_t *self, uint32_t instance, wl_transport_t *transport, const device_peer_handlers_t *handlers, void *user);
wl_err_t device_peer_bind_sync(device_peer_node_t *self, device_peer_tx_write_fn tx_write, const device_peer_handlers_t *handlers, void *user);
wl_err_t device_peer_bind_sync_instance(device_peer_node_t *self, uint32_t instance, device_peer_tx_write_fn tx_write, const device_peer_handlers_t *handlers, void *user);
#ifdef __cplusplus
}
#endif

#endif
