/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_GENERATED_PEER_H
#define WIRELINK_GENERATED_PEER_H

#if !defined(__cplusplus)
#include <stdbool.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include <wirelink/codec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct peer_add_request peer_add_request_t;
typedef struct peer_add_response peer_add_response_t;

struct peer_add_request {
  int32_t left;
  int32_t right;
};

struct peer_add_response {
  int32_t sum;
};

#define PEER_ADD_REQUEST_MESSAGE_ID 20U
#define PEER_ADD_REQUEST_CODEC_PROFILE 1U
#define PEER_ADD_REQUEST_CODEC_FORMAT "schema-v1"
#define PEER_ADD_REQUEST_HAS_MAX_ENCODED_SIZE 1
#define PEER_ADD_REQUEST_MAX_ENCODED_SIZE UINT64_C(12)
void peer_add_request_clear(peer_add_request_t *value);
wl_codec_status_t peer_add_request_decode(const uint8_t *input, size_t input_length, peer_add_request_t *out);

#define PEER_ADD_RESPONSE_MESSAGE_ID 21U
#define PEER_ADD_RESPONSE_CODEC_PROFILE 1U
#define PEER_ADD_RESPONSE_CODEC_FORMAT "schema-v1"
#define PEER_ADD_RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define PEER_ADD_RESPONSE_MAX_ENCODED_SIZE UINT64_C(6)
void peer_add_response_clear(peer_add_response_t *value);
size_t peer_add_response_encoded_size(const peer_add_response_t *value);
wl_codec_status_t peer_add_response_encode(const peer_add_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);

#include <wirelink/node.h>
#include <wirelink/version.h>
#if WIRELINK_CODEGEN_CONTRACT != 1
#error "WLC requires generated-code contract 1"
#endif

typedef struct calculator_peer_node calculator_peer_node_t;
#define CALCULATOR_PEER_REQUEST_FRAME_MAX 0U
#define CALCULATOR_PEER_REPLY_FRAME_MAX 18U
#define CALCULATOR_PEER_MESSAGE_FRAME_MAX 0U
#define CALCULATOR_PEER_RX_FRAME_MAX 24U
#define CALCULATOR_PEER_TX_FRAME_MAX 18U
#define CALCULATOR_PEER_RX_WIRE_MAX 24U
#define CALCULATOR_PEER_TX_WIRE_MAX 18U
/* Accept and drain the whole frame, or reject it without consuming bytes. */
typedef wl_err_t (*calculator_peer_tx_write_fn)(calculator_peer_node_t *self, const uint8_t *data, size_t length);
typedef struct {
  int32_t (*on_add)(void *, const peer_add_request_t *, peer_add_response_t *);
} calculator_peer_handlers_t;
struct calculator_peer_node {
  wl_node_t node;
  calculator_peer_handlers_t handlers;
  union {
    peer_add_response_t add;
  } reply;
  uint8_t replies[WL_ENDPOINT_REPLIES][18];
  uint8_t rx[WL_PACKET_RX_SLOTS][CALCULATOR_PEER_RX_WIRE_MAX + 1U];
  calculator_peer_tx_write_fn tx_write;
};
/* Explicit instance IDs must be nonzero. Rebind only after close completes. */
wl_err_t calculator_peer_bind(calculator_peer_node_t *self, wl_transport_t *transport, const calculator_peer_handlers_t *handlers, void *user);
wl_err_t calculator_peer_bind_instance(calculator_peer_node_t *self, uint32_t instance, wl_transport_t *transport, const calculator_peer_handlers_t *handlers, void *user);
wl_err_t calculator_peer_bind_sync(calculator_peer_node_t *self, calculator_peer_tx_write_fn tx_write, const calculator_peer_handlers_t *handlers, void *user);
wl_err_t calculator_peer_bind_sync_instance(calculator_peer_node_t *self, uint32_t instance, calculator_peer_tx_write_fn tx_write, const calculator_peer_handlers_t *handlers, void *user);
#ifdef __cplusplus
}
#endif

#endif
