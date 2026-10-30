#ifndef WIRELINK_GENERATED_DEMO_H
#define WIRELINK_GENERATED_DEMO_H

/* Advanced borrowed codec and explicit value/view conversions. */
#include "demo_values.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct request request_t;
typedef struct response response_t;
typedef struct event event_t;

struct request {
  bool has_input;
  int32_t input;
};

struct response {
  bool has_output;
  int32_t output;
};

struct event {
  bool has_data;
  wl_codec_bytes_t data;
};

void request_clear(request_t *value);
size_t request_encoded_size(const request_t *value);
wl_codec_status_t request_encode(const request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t request_decode(const uint8_t *input, size_t input_length, request_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t request_value_from_view(const request_t *view, request_value_t *out);
wl_codec_status_t request_value_to_view(const request_value_t *value, request_t *out);

void response_clear(response_t *value);
size_t response_encoded_size(const response_t *value);
wl_codec_status_t response_encode(const response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t response_decode(const uint8_t *input, size_t input_length, response_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t response_value_from_view(const response_t *view, response_value_t *out);
wl_codec_status_t response_value_to_view(const response_value_t *value, response_t *out);

void event_clear(event_t *value);
size_t event_encoded_size(const event_t *value);
wl_codec_status_t event_encode(const event_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t event_decode(const uint8_t *input, size_t input_length, event_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t event_value_from_view(const event_t *view, event_value_t *out);
wl_codec_status_t event_value_to_view(const event_value_t *value, event_t *out);

#ifdef __cplusplus
}
#endif

#endif
