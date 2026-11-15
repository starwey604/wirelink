/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_GENERATED_CALCULATOR_H
#define WIRELINK_GENERATED_CALCULATOR_H

#if !defined(__cplusplus)
#include <stdbool.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include <wirelink/codec.h>

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

#define ADD_REQUEST_MESSAGE_ID 20U
#define ADD_REQUEST_CODEC_PROFILE 1U
#define ADD_REQUEST_CODEC_FORMAT "schema-v1"
#define ADD_REQUEST_HAS_MAX_ENCODED_SIZE 1
#define ADD_REQUEST_MAX_ENCODED_SIZE UINT64_C(12)
void add_request_clear(add_request_t *value);
size_t add_request_encoded_size(const add_request_t *value);
wl_codec_status_t add_request_encode(const add_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t add_request_decode(const uint8_t *input, size_t input_length, add_request_t *out);

#define ADD_RESPONSE_MESSAGE_ID 21U
#define ADD_RESPONSE_CODEC_PROFILE 1U
#define ADD_RESPONSE_CODEC_FORMAT "schema-v1"
#define ADD_RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define ADD_RESPONSE_MAX_ENCODED_SIZE UINT64_C(6)
void add_response_clear(add_response_t *value);
size_t add_response_encoded_size(const add_response_t *value);
wl_codec_status_t add_response_encode(const add_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t add_response_decode(const uint8_t *input, size_t input_length, add_response_t *out);

#ifdef __cplusplus
}
#endif

#endif
