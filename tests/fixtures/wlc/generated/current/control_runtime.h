#ifndef WIRELINK_GENERATED_CONTROL_RUNTIME_H
#define WIRELINK_GENERATED_CONTROL_RUNTIME_H

#include "control_bindings.h"
#include <wirelink/fifo.h>
#include <wirelink/latest.h>
#include <wirelink/rpc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROL_SCHEMA_IDENTITY UINT64_C(0x3E4D43CC60BB9E4E)
#define CONTROL_BINDING_PROFILE_IDENTITY UINT64_C(0x3BAB7A3869C4D89E)
#define CONTROL_BINDING_PROFILE_VERSION 1U
#define CONTROL_IDENTITY_ALGORITHM "fnv1a64-v1"

#define CONTROL_RPC_REQUEST_FINGERPRINT_ALGORITHM "fnv1a64-canonical-request-v1"

typedef int32_t control_runtime_domain_t;
enum {
  CONTROL_RUNTIME_OK = 0,
  CONTROL_RUNTIME_NON_RX,
  CONTROL_RUNTIME_UNKNOWN_MESSAGE,
  CONTROL_RUNTIME_MISSING_ROUTE,
  CONTROL_RUNTIME_MISSING_SCRATCH,
  CONTROL_RUNTIME_DELIVERY_MISMATCH,
  CONTROL_RUNTIME_CODEC_ERROR,
  CONTROL_RUNTIME_STORAGE_ERROR,
  CONTROL_RUNTIME_RPC_ERROR,
  CONTROL_RUNTIME_CORE_ERROR,
  CONTROL_RUNTIME_APPLICATION_ERROR,
  CONTROL_RUNTIME_INVALID_ARGUMENT
};

typedef struct {
  control_runtime_domain_t domain;
  uint16_t message_id;
  wl_event_type_t event_type;
  wl_codec_status_t codec_status;
  int32_t storage_result;
  int32_t abort_result;
  wl_rpc_err_t rpc_result;
  int32_t core_result;
  int32_t application_result;
  wl_rpc_server_disposition_t rpc_disposition;
  uint32_t operation_id;
  wl_tx_handle_t handle;
  size_t payload_length;
  wl_rpc_server_response_t server_response;
} control_runtime_result_t;

/* The decoded request and borrowed fields are valid only for the callback.
 * Return zero after copying anything needed asynchronously. A nonzero return
 * abandons the pending operation without manufacturing a response. */
typedef int32_t (*control_home_request_handler_fn)(void *user_data, const home_request_t *request, wl_delivery_t delivery);
typedef struct {
  home_request_t *request_scratch;
  home_response_t *response_scratch;
  control_encode_scratch_t canonical_request_scratch;
  control_home_request_handler_fn request_handler;
  void *user_data;
} control_home_rpc_t;

typedef struct {
  uint8_t _reserved;
  wl_fifo_t *joint_command_fifo;
  wl_latest_t *arm_mit_command_latest;
  wl_rpc_client_t *rpc_client;
  wl_rpc_server_t *rpc_server;
  control_home_rpc_t home;
} control_runtime_t;

/* Terminal consumer for RX events: with non-null ctx/event every RX
 * outcome releases the event exactly once. Do not chain another dispatcher
 * or release it again. Non-RX events are never released; matching RPC TX
 * terminal events advance the client but the caller still owns wl_tx_take(). */
control_runtime_result_t control_runtime_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, control_runtime_t *runtime, wl_time_ms_t now_ms);

/* Client start writes the allocated operation ID into request in place. */
control_runtime_result_t control_home_client_start_scratch(wl_ctx_t *ctx, control_runtime_t *runtime, home_request_t *request, uint32_t timeout_ms, wl_time_ms_t now_ms, control_encode_scratch_t scratch);
control_runtime_result_t control_home_client_start_direct(wl_ctx_t *ctx, control_runtime_t *runtime, home_request_t *request, uint32_t timeout_ms, wl_time_ms_t now_ms);

/* Completion writes operation ID and status into response in place, encodes
 * once, caches those bytes, and sends the exact cached byte sequence. */
control_runtime_result_t control_home_server_complete(wl_ctx_t *ctx, control_runtime_t *runtime, uint32_t operation_id, home_response_t *response, control_encode_scratch_t scratch, wl_time_ms_t now_ms);
control_runtime_result_t control_home_server_reject(wl_ctx_t *ctx, control_runtime_t *runtime, uint32_t operation_id, int32_t application_status, home_response_t *response, control_encode_scratch_t scratch, wl_time_ms_t now_ms);
/* cached.response_data remains owned by wl_rpc_server_t; send or copy it before
 * the next server mutation, poll, or expiry. */
control_runtime_result_t control_home_server_retry_cached(wl_ctx_t *ctx, const wl_rpc_server_response_t *cached);

#ifdef __cplusplus
}
#endif

#endif
