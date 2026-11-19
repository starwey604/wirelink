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
  int32_t input;
};

struct response {
  int32_t output;
};

struct event {
  struct { size_t length; uint8_t data[9]; } data;
};

static inline bool event_set_data(event_t *msg, const void *value, size_t length) {
  if (msg == NULL || !wl_codec_inline_bytes_set(&msg->data.length, msg->data.data, 9U, value, length)) return false;
  return true;
}

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

#include <wirelink/endpoint.h>
#include <wirelink/version.h>
#if WIRELINK_CODEGEN_CONTRACT != 1
#error "WLC requires generated-code contract 1"
#endif

typedef struct demo_node demo_node_t;
#define DEMO_REQUEST_FRAME_MAX 18U
#define DEMO_REPLY_FRAME_MAX 18U
#define DEMO_MESSAGE_FRAME_MAX 15U
#define DEMO_RX_FRAME_MAX 18U
#define DEMO_TX_FRAME_MAX 18U
#define DEMO_RX_WIRE_MAX 18U
#define DEMO_TX_WIRE_MAX 18U
typedef struct {
  int32_t (*on_execute)(void *, const request_t *, response_t *);
  void (*on_event)(void *, const event_t *);
} demo_handlers_t;
struct demo_node {
  wl_endpoint_t core;
  void *user;
  wl_endpoint_error_fn on_error;
  demo_handlers_t handlers;
  union {
    response_t execute;
  } reply;
  uint8_t replies[WL_ENDPOINT_REPLIES][18];
  uint8_t requests[WL_ENDPOINT_CALLS][18];
  uint8_t message[15];
};
wl_err_t demo_init(demo_node_t *self, wl_packet_port_t *port, const demo_handlers_t *handlers, void *user);
wl_err_t demo_step(demo_node_t *self, uint32_t now);
/* Stop the RX producer before close, then step until quiescent. */
wl_err_t demo_close(demo_node_t *self);
int demo_quiescent(const demo_node_t *self);
wl_err_t demo_cancel(demo_node_t *self, wl_call_handle_t handle);
wl_err_t demo_send_event(demo_node_t *self, const event_t *message);
typedef void (*demo_execute_cb_t)(demo_node_t *, wl_rpc_result_t, const response_t *);
typedef void (*demo_execute_ex_cb_t)(void *, wl_rpc_result_t, const response_t *);
/* Simplified calls start their timeout on the next step(now). */
wl_err_t demo_call_execute(demo_node_t *self, const request_t *request, demo_execute_cb_t callback);
wl_err_t demo_call_execute_ex(demo_node_t *self, const request_t *request, uint32_t now, uint32_t timeout_ms, demo_execute_ex_cb_t callback, void *user, wl_call_handle_t *out);
#ifdef __cplusplus
}
#endif

#endif
