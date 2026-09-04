#ifndef WIRELINK_GENERATED_CONTROL_RUNTIME_H
#define WIRELINK_GENERATED_CONTROL_RUNTIME_H

#include "control_bindings.h"
#include <wirelink/fifo.h>
#include <wirelink/latest.h>
#include <wirelink/rpc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROL_SCHEMA_IDENTITY UINT64_C(0x0048B48181A18D13)
#define CONTROL_BINDING_PROFILE_IDENTITY UINT64_C(0x3BAB7A3869C4D89E)
#define CONTROL_BINDING_PROFILE_VERSION 1U
#define CONTROL_IDENTITY_ALGORITHM "fnv1a64-v1"

#define CONTROL_RUNTIME_CODEGEN_ABI_VERSION 13U

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

typedef uint8_t control_runtime_detail_kind_t;
enum {
  CONTROL_RUNTIME_DETAIL_NONE = 0,
  CONTROL_RUNTIME_DETAIL_RETAINED = 1,
  CONTROL_RUNTIME_DETAIL_RPC = 2
};

typedef struct {
  wl_codec_status_t codec_status;
  int32_t storage_result;
  int32_t abort_result;
} control_runtime_retained_detail_t;

typedef struct {
  wl_codec_status_t codec_status;
  wl_rpc_err_t rpc_result;
  int32_t core_result;
  int32_t application_result;
  wl_rpc_server_disposition_t rpc_disposition;
  uint32_t operation_id;
  wl_tx_handle_t handle;
  size_t payload_length;
  union {
    wl_rpc_server_request_t server_request;
    wl_rpc_server_response_t server_response;
  };
} control_runtime_rpc_detail_t;

typedef union {
  control_runtime_retained_detail_t retained;
  control_runtime_rpc_detail_t rpc;
} control_runtime_detail_t;

/* Inspect detail only through the member selected by detail_kind. domain
 * classifies the outcome; zero-initialized unused detail fields retain their
 * corresponding success values. */
typedef struct {
  control_runtime_domain_t domain;
  wl_event_type_t event_type;
  uint16_t message_id;
  control_runtime_detail_kind_t detail_kind;
  uint8_t _reserved;
  control_runtime_detail_t detail;
} control_runtime_result_t;

/* The typed value remains borrowed until the matching release. */
typedef struct {
  const joint_command_t *value;
  wl_fifo_view_t lease;
} control_joint_command_fifo_view_t;

/* The typed value remains borrowed until the matching release. */
typedef struct {
  const arm_mit_command_t *value;
  uint32_t generation;
  wl_latest_view_t lease;
} control_arm_mit_command_latest_view_t;

/* The decoded request and its borrowed fields are valid only for the
 * callback. Copy server_request for asynchronous completion. Its generation
 * prevents a late completion from targeting a reused request identity. A
 * nonzero return abandons this exact pending operation. */
typedef int32_t (*control_home_rpc_request_handler_fn)(void *user_data, const home_request_t *request, const wl_rpc_server_request_t *server_request, wl_delivery_t delivery);
typedef struct {
  home_request_t *request_scratch;
  home_response_t *response_scratch;
  control_encode_scratch_t canonical_request_scratch;
  control_home_rpc_request_handler_fn request_handler;
  void *user_data;
} control_home_rpc_t;

typedef struct {
  uint16_t client_timed_out;
  uint16_t server_pending_expired;
  uint16_t server_cache_expired;
  wl_rpc_server_request_t server_expired_request;
} control_runtime_poll_result_t;

typedef struct {
  control_runtime_poll_result_t deadlines;
  control_runtime_result_t response;
  uint16_t responses_submitted;
  uint16_t responses_deferred;
} control_runtime_service_result_t;

typedef struct {
  uint8_t _reserved;
  wl_fifo_t *joint_command_fifo;
  wl_latest_t *arm_mit_command_latest;
  wl_rpc_client_t *rpc_client;
  wl_rpc_server_t *rpc_server;
  control_home_rpc_t home;
} control_runtime_t;

/* Static runtime assembly. requirements() validates every sizing field and
 * reports the exact caller-owned byte storage needed by init(). Configuration
 * and storage descriptors may be temporary; instance and storage must outlive
 * all runtime activity and must not be copied after successful initialization. */
typedef struct {
  uint8_t _reserved;
  uint32_t joint_command_fifo_capacity;
  uint32_t arm_mit_command_latest_initial_generation;
  uint8_t rpc_client_enabled;
  uint16_t rpc_client_slot_count;
  uint16_t rpc_client_response_capacity;
  uint32_t rpc_client_next_operation_id;
  uint8_t rpc_server_enabled;
  uint16_t rpc_server_pending_slot_count;
  uint16_t rpc_server_cache_slot_count;
  uint16_t rpc_server_response_capacity;
  uint32_t rpc_server_pending_timeout_ms;
  uint32_t rpc_server_cache_ttl_ms;
  wl_rpc_cache_policy_t rpc_server_cache_policy;
  size_t home_canonical_request_capacity;
  control_home_rpc_request_handler_fn home_request_handler;
  void *home_user_data;
} control_runtime_config_t;

typedef struct {
  size_t storage_size;
  size_t storage_alignment;
} control_runtime_requirements_t;

typedef struct {
  void *data;
  size_t size;
} control_runtime_storage_t;

typedef struct {
  control_runtime_t runtime;
  wl_fifo_t joint_command_fifo;
  wl_latest_t arm_mit_command_latest;
  wl_rpc_client_t rpc_client;
  wl_rpc_server_t rpc_server;
  /* Dispatch is serialized; request and response decode scratch lifetimes do not overlap. */
  union { home_request_t request; home_response_t response; } home_scratch;
} control_runtime_instance_t;

int control_runtime_requirements(const control_runtime_config_t *config, control_runtime_requirements_t *out_requirements);
int control_runtime_init(control_runtime_instance_t *instance, const control_runtime_config_t *config, const control_runtime_storage_t *storage);

/* Terminal consumer for RX events: with non-null ctx/event every RX
 * outcome releases the event exactly once. Do not chain another dispatcher
 * or release it again. Matching RPC TX terminal events advance the runtime
 * and reclaim the handle. Unmatched non-RX events remain caller-owned. */
control_runtime_result_t control_runtime_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, control_runtime_t *runtime, wl_time_ms_t now_ms);

int control_joint_command_fifo_acquire(control_runtime_t *runtime, control_joint_command_fifo_view_t *out_view);
int control_joint_command_fifo_release(control_runtime_t *runtime, control_joint_command_fifo_view_t *view);

int control_arm_mit_command_latest_acquire(control_runtime_t *runtime, control_arm_mit_command_latest_view_t *out_view);
int control_arm_mit_command_latest_release(control_runtime_t *runtime, control_arm_mit_command_latest_view_t *view);

/* Advance configured RPC deadlines without performing I/O. At most one
 * expired server identity is returned per call and remains pending until the
 * application completes, rejects, or abandons it. */
wl_rpc_err_t control_runtime_poll(control_runtime_t *runtime, wl_time_ms_t now_ms, control_runtime_poll_result_t *out_result);
/* Advance deadlines and submit at most one runtime-owned server response.
 * Link backpressure defers the same cached bytes for a later service call. */
wl_rpc_err_t control_runtime_service(wl_ctx_t *ctx, control_runtime_t *runtime, wl_time_ms_t now_ms, control_runtime_service_result_t *out_result);
/* Side-effect free. Zero is due; WL_RPC_NO_DEADLINE_MS means no deadline. */
wl_rpc_err_t control_runtime_get_deadline_hint(const control_runtime_t *runtime, wl_time_ms_t now_ms, wl_rpc_deadline_hint_t *out_hint);

/* Allocates, encodes, and submits atomically from the caller's view. The
 * request is restored before return. A local encode/submit failure releases the
 * allocated RPC slot and returns operation_id zero. */
control_runtime_result_t control_home_client_start(wl_ctx_t *ctx, control_runtime_t *runtime, home_request_t *request, uint32_t timeout_ms, wl_time_ms_t now_ms);
/* Nonblocking inspection returns generic metadata for this service. */
wl_rpc_err_t control_home_client_inspect(const control_runtime_t *runtime, uint32_t operation_id, wl_rpc_client_result_t *out_client);
/* Decode a retained response previously returned by client_inspect(). Borrowed
 * response fields remain valid only until client_release(). */
control_runtime_result_t control_home_client_decode(const wl_rpc_client_result_t *client, home_response_t *response);
wl_rpc_err_t control_home_client_release(control_runtime_t *runtime, uint32_t operation_id);

/* server_request is copied from the request callback and uniquely scopes this
 * execution generation. Completion encodes directly into the response storage
 * reserved by wl_rpc_server_begin(); runtime_service() performs I/O. */
control_runtime_result_t control_home_server_complete(control_runtime_t *runtime, const wl_rpc_server_request_t *server_request, home_response_t *response, wl_time_ms_t now_ms);
control_runtime_result_t control_home_server_reject(control_runtime_t *runtime, const wl_rpc_server_request_t *server_request, int32_t application_status, home_response_t *response, wl_time_ms_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
