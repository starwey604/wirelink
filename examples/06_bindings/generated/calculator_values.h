#ifndef WIRELINK_GENERATED_CALCULATOR_H_VALUES
#define WIRELINK_GENERATED_CALCULATOR_H_VALUES

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
#define ADD_REQUEST_HAS_VALUE 1
typedef struct {
  bool has_left;
  int32_t left;
  bool has_right;
  int32_t right;
} add_request_value_t;
#define ADD_REQUEST_VALUE_SIZE (sizeof(add_request_value_t))
void add_request_value_clear(add_request_value_t *value);
size_t add_request_value_encoded_size(const add_request_value_t *value);
wl_codec_status_t add_request_value_encode(const add_request_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t add_request_value_decode(const uint8_t *input, size_t length, add_request_value_t *out);

#define ADD_RESPONSE_HAS_VALUE 1
typedef struct {
  bool has_sum;
  int32_t sum;
} add_response_value_t;
#define ADD_RESPONSE_VALUE_SIZE (sizeof(add_response_value_t))
void add_response_value_clear(add_response_value_t *value);
size_t add_response_value_encoded_size(const add_response_value_t *value);
wl_codec_status_t add_response_value_encode(const add_response_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t add_response_value_decode(const uint8_t *input, size_t length, add_response_value_t *out);

#define ADD_REQUEST_MESSAGE_ID 20U
#define ADD_REQUEST_HAS_MAX_ENCODED_SIZE 1
#define ADD_REQUEST_MAX_ENCODED_SIZE UINT64_C(12)
#define ADD_RESPONSE_MESSAGE_ID 21U
#define ADD_RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define ADD_RESPONSE_MAX_ENCODED_SIZE UINT64_C(6)
#ifdef __cplusplus
}
#endif

#endif
