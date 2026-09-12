#ifndef WIRELINK_GENERATED_CALCULATOR_H
#define WIRELINK_GENERATED_CALCULATOR_H

/* Advanced borrowed codec and explicit value/view conversions. */
#include "calculator_values.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct add_request add_request_t;
typedef struct add_response add_response_t;

struct add_request {
  bool has_left;
  int32_t left;
  bool has_right;
  int32_t right;
};

struct add_response {
  bool has_sum;
  int32_t sum;
};

void add_request_clear(add_request_t *value);
size_t add_request_encoded_size(const add_request_t *value);
wl_codec_status_t add_request_encode(const add_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t add_request_decode(const uint8_t *input, size_t input_length, add_request_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t add_request_value_from_view(const add_request_t *view, add_request_value_t *out);
wl_codec_status_t add_request_value_to_view(const add_request_value_t *value, add_request_t *out);

void add_response_clear(add_response_t *value);
size_t add_response_encoded_size(const add_response_t *value);
wl_codec_status_t add_response_encode(const add_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t add_response_decode(const uint8_t *input, size_t input_length, add_response_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t add_response_value_from_view(const add_response_t *view, add_response_value_t *out);
wl_codec_status_t add_response_value_to_view(const add_response_value_t *value, add_response_t *out);

#ifdef __cplusplus
}
#endif

#endif
