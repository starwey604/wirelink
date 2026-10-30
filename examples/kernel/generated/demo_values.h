#ifndef WIRELINK_GENERATED_DEMO_H_VALUES
#define WIRELINK_GENERATED_DEMO_H_VALUES

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wirelink/codec.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Self-owning business values. Assignment copies all data; no destroy is needed.
 * String lengths are bytes (embedded NUL is allowed); data[length] is a
 * convenience terminator after clear/decode/from_view, not part of the wire.
 * Views borrow their source. Conversion input/output must not overlap.
 * Failed conversions/decodes leave output unchanged. */
#define REQUEST_HAS_VALUE 1
typedef struct {
  bool has_input;
  int32_t input;
} request_value_t;
#define REQUEST_VALUE_SIZE (sizeof(request_value_t))
void request_value_clear(request_value_t *value);
size_t request_value_encoded_size(const request_value_t *value);
wl_codec_status_t request_value_encode(const request_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t request_value_decode(const uint8_t *input, size_t length, request_value_t *out);

#define RESPONSE_HAS_VALUE 1
typedef struct {
  bool has_output;
  int32_t output;
} response_value_t;
#define RESPONSE_VALUE_SIZE (sizeof(response_value_t))
void response_value_clear(response_value_t *value);
size_t response_value_encoded_size(const response_value_t *value);
wl_codec_status_t response_value_encode(const response_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t response_value_decode(const uint8_t *input, size_t length, response_value_t *out);

#define EVENT_HAS_VALUE 1
typedef struct {
  bool has_data;
  struct { size_t length; uint8_t data[9]; } data;
} event_value_t;
#define EVENT_VALUE_SIZE (sizeof(event_value_t))
void event_value_clear(event_value_t *value);
size_t event_value_encoded_size(const event_value_t *value);
wl_codec_status_t event_value_encode(const event_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t event_value_decode(const uint8_t *input, size_t length, event_value_t *out);

#define REQUEST_CODEC_PROFILE 1U
#define REQUEST_CODEC_FORMAT "schema-v1"
#define REQUEST_MESSAGE_ID 40U
#define REQUEST_HAS_MAX_ENCODED_SIZE 1
#define REQUEST_MAX_ENCODED_SIZE UINT64_C(6)
#define RESPONSE_CODEC_PROFILE 1U
#define RESPONSE_CODEC_FORMAT "schema-v1"
#define RESPONSE_MESSAGE_ID 41U
#define RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define RESPONSE_MAX_ENCODED_SIZE UINT64_C(6)
#define EVENT_CODEC_PROFILE 1U
#define EVENT_CODEC_FORMAT "schema-v1"
#define EVENT_MESSAGE_ID 42U
#define EVENT_HAS_MAX_ENCODED_SIZE 1
#define EVENT_MAX_ENCODED_SIZE UINT64_C(11)
#ifdef __cplusplus
}
#endif

#endif
