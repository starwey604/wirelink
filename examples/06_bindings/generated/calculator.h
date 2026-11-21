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
  int32_t left;
  int32_t right;
};

struct add_response {
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

#include <wirelink/endpoint.h>
#include <wirelink/version.h>
#if WIRELINK_CODEGEN_CONTRACT != 2
#error "WLC requires generated-code contract 2"
#endif

typedef struct calculator_node calculator_node_t;
#define CALCULATOR_REQUEST_FRAME_MAX 24U
#define CALCULATOR_REPLY_FRAME_MAX 0U
#define CALCULATOR_MESSAGE_FRAME_MAX 0U
#define CALCULATOR_RX_FRAME_MAX 18U
#define CALCULATOR_TX_FRAME_MAX 24U
#define CALCULATOR_RX_WIRE_MAX 18U
#define CALCULATOR_TX_WIRE_MAX 24U
/* Accept and drain the whole frame, or reject it without consuming bytes. */
typedef wl_err_t (*calculator_tx_write_fn)(calculator_node_t *self, const uint8_t *data, size_t length);
struct calculator_node {
  wl_endpoint_t core;
  void *user;
  wl_endpoint_error_fn on_error;
  uint8_t requests[WL_ENDPOINT_CALLS][24];
  wl_packet_port_t port;
  uint8_t rx[WL_PACKET_RX_SLOTS][CALCULATOR_RX_WIRE_MAX + 1U];
  calculator_tx_write_fn tx_write;
};
/* Automatic instances are unique only within this process lifetime.
 * Stop I/O before reinitialization; persistent links should supply an instance. */
wl_err_t calculator_init(calculator_node_t *self, calculator_tx_write_fn tx_write);
wl_err_t calculator_init_with_instance(calculator_node_t *self, uint32_t instance, calculator_tx_write_fn tx_write);
/* Instance zero requests an automatic instance. */
wl_err_t calculator_init_io(calculator_node_t *self, uint32_t instance, const wl_packet_io_t *io);
static inline void calculator_tx_ready(calculator_node_t *self) { wl_packet_activity(&self->port); }
wl_err_t calculator_rx_packet(calculator_node_t *self, const uint8_t *data, size_t length);
wl_err_t calculator_step(calculator_node_t *self, uint32_t now);
/* Stop the RX producer before close, then step until quiescent. */
wl_err_t calculator_close(calculator_node_t *self);
int calculator_quiescent(const calculator_node_t *self);
wl_err_t calculator_cancel(calculator_node_t *self, wl_call_handle_t handle);
typedef void (*calculator_add_cb_t)(calculator_node_t *, wl_rpc_result_t, const add_response_t *);
typedef void (*calculator_add_ex_cb_t)(void *, wl_rpc_result_t, const add_response_t *);
/* Simplified calls start their timeout on the next step(now). */
wl_err_t calculator_call_add(calculator_node_t *self, const add_request_t *request, calculator_add_cb_t callback);
wl_err_t calculator_call_add_ex(calculator_node_t *self, const add_request_t *request, uint32_t now, uint32_t timeout_ms, calculator_add_ex_cb_t callback, void *user, wl_call_handle_t *out);
#ifdef __cplusplus
}
#endif

#endif
