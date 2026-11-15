/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_GENERATED_DEMO_H
#define WIRELINK_GENERATED_DEMO_H

#if !defined(__cplusplus)
#include <stdbool.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include <wirelink/codec.h>

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
  struct { size_t length; uint8_t data[9]; } data;
};

#define REQUEST_MESSAGE_ID 40U
#define REQUEST_CODEC_PROFILE 1U
#define REQUEST_CODEC_FORMAT "schema-v1"
#define REQUEST_HAS_MAX_ENCODED_SIZE 1
#define REQUEST_MAX_ENCODED_SIZE UINT64_C(6)
void request_clear(request_t *value);
size_t request_encoded_size(const request_t *value);
wl_codec_status_t request_encode(const request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t request_decode(const uint8_t *input, size_t input_length, request_t *out);

#define RESPONSE_MESSAGE_ID 41U
#define RESPONSE_CODEC_PROFILE 1U
#define RESPONSE_CODEC_FORMAT "schema-v1"
#define RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define RESPONSE_MAX_ENCODED_SIZE UINT64_C(6)
void response_clear(response_t *value);
size_t response_encoded_size(const response_t *value);
wl_codec_status_t response_encode(const response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t response_decode(const uint8_t *input, size_t input_length, response_t *out);

#define EVENT_MESSAGE_ID 42U
#define EVENT_CODEC_PROFILE 1U
#define EVENT_CODEC_FORMAT "schema-v1"
#define EVENT_HAS_MAX_ENCODED_SIZE 1
#define EVENT_MAX_ENCODED_SIZE UINT64_C(11)
void event_clear(event_t *value);
size_t event_encoded_size(const event_t *value);
wl_codec_status_t event_encode(const event_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t event_decode(const uint8_t *input, size_t input_length, event_t *out);

#ifdef __cplusplus
}
#endif

#endif
