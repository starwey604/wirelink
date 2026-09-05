#ifndef WIRELINK_GENERATED_CONTROL_RUNTIME_H
#define WIRELINK_GENERATED_CONTROL_RUNTIME_H

#include "control_bindings.h"
#include <wirelink/pump.h>
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

#define CONTROL_RUNTIME_CODEGEN_ABI_VERSION 18U

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
  uint8_t peer_changed;
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
 * corresponding success values. event_consumed is nonzero only when dispatch
 * released an RX event or reclaimed a terminal TX handle. */
typedef struct {
  control_runtime_domain_t domain;
  wl_event_type_t event_type;
  uint16_t message_id;
  control_runtime_detail_kind_t detail_kind;
  uint8_t event_consumed;
  control_runtime_detail_t detail;
} control_runtime_result_t;

/* Convenience helpers preserve the full diagnostic result. Detail accessors
 * return null unless detail_kind selects the requested member. Result strings
 * are diagnostic text and must not be parsed as a stable machine interface. */
static inline bool control_runtime_result_ok(const control_runtime_result_t *result) {
  return result != NULL && result->domain == CONTROL_RUNTIME_OK;
}

const char *control_runtime_result_str(const control_runtime_result_t *result);

static inline const control_runtime_retained_detail_t *control_runtime_result_retained_detail(const control_runtime_result_t *result) {
  return result != NULL && result->detail_kind == CONTROL_RUNTIME_DETAIL_RETAINED ? &result->detail.retained : NULL;
}

static inline const control_runtime_rpc_detail_t *control_runtime_result_rpc_detail(const control_runtime_result_t *result) {
  return result != NULL && result->detail_kind == CONTROL_RUNTIME_DETAIL_RPC ? &result->detail.rpc : NULL;
}

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

/* Shared by synchronous RPC encoders. Runtime APIs are owner-thread
 * operations and do not retain pointers to this scratch after return. */
typedef union {
  home_request_t home_request;
  home_response_t home_response;
} control_runtime_rpc_encode_scratch_t;

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
  wl_rpc_peer_t rpc_peer;
  wl_rpc_peer_observation_t rpc_peer_observation;
  control_runtime_rpc_encode_scratch_t *rpc_encode_scratch;
  control_home_rpc_t home;
} control_runtime_t;

typedef void (*control_runtime_result_fn)(void *user_data, const control_runtime_result_t *result);

typedef struct {
  control_runtime_t *runtime;
  void *user_data;
  control_runtime_result_fn on_result;
  wl_rpc_err_t last_service_result;
  control_runtime_service_result_t last_service;
} control_runtime_pump_t;

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

#define CONTROL_RUNTIME_HAS_DEFAULT_STORAGE 1
typedef union {
  uint8_t byte;
  joint_command_t joint_command_fifo;
  arm_mit_command_t arm_mit_command_latest;
  wl_rpc_client_slot_t rpc_client_slot;
  wl_rpc_server_pending_slot_t rpc_server_pending_slot;
  wl_rpc_server_cache_slot_t rpc_server_cache_slot;
} control_runtime_default_storage_alignment_t;

#if defined(__cplusplus)
#define CONTROL_RUNTIME_DEFAULT_STORAGE_ALIGNMENT alignof(control_runtime_default_storage_alignment_t)
#elif defined(_MSC_VER)
#define CONTROL_RUNTIME_DEFAULT_STORAGE_ALIGNMENT __alignof(control_runtime_default_storage_alignment_t)
#else
#define CONTROL_RUNTIME_DEFAULT_STORAGE_ALIGNMENT _Alignof(control_runtime_default_storage_alignment_t)
#endif
#define CONTROL_RUNTIME_DEFAULT_STORAGE_CAPACITY \
  (1U + \
   ((CONTROL_RUNTIME_DEFAULT_STORAGE_ALIGNMENT - 1U) + sizeof(joint_command_t) + (CONTROL_RUNTIME_DEFAULT_STORAGE_ALIGNMENT - 1U)) + \
   ((CONTROL_RUNTIME_DEFAULT_STORAGE_ALIGNMENT - 1U) + ((sizeof(arm_mit_command_t) + (CONTROL_RUNTIME_DEFAULT_STORAGE_ALIGNMENT - 1U)) * WL_LATEST_SLOT_COUNT)) + \
   ((CONTROL_RUNTIME_DEFAULT_STORAGE_ALIGNMENT - 1U) + sizeof(wl_rpc_client_slot_t)) + \
   12U + \
   ((CONTROL_RUNTIME_DEFAULT_STORAGE_ALIGNMENT - 1U) + sizeof(wl_rpc_server_pending_slot_t)) + \
   ((CONTROL_RUNTIME_DEFAULT_STORAGE_ALIGNMENT - 1U) + sizeof(wl_rpc_server_cache_slot_t)) + \
   12U + \
   12U)

typedef union {
  control_runtime_default_storage_alignment_t alignment;
  uint8_t bytes[CONTROL_RUNTIME_DEFAULT_STORAGE_CAPACITY];
} control_runtime_default_storage_t;

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
  control_runtime_rpc_encode_scratch_t rpc_encode_scratch;
  /* Dispatch is serialized; request and response decode scratch lifetimes do not overlap. */
  union { home_request_t request; home_response_t response; } home_scratch;
} control_runtime_instance_t;

typedef int32_t control_runtime_init_issue_t;
enum {
  CONTROL_RUNTIME_INIT_OK = 0,
  CONTROL_RUNTIME_INIT_NULL_ARGUMENT,
  CONTROL_RUNTIME_INIT_ROLE_ENABLE,
  CONTROL_RUNTIME_INIT_RETAINED_CAPACITY,
  CONTROL_RUNTIME_INIT_RPC_CLIENT_CAPACITY,
  CONTROL_RUNTIME_INIT_RPC_SERVER_CAPACITY,
  CONTROL_RUNTIME_INIT_RPC_TIMEOUT,
  CONTROL_RUNTIME_INIT_RPC_CACHE_POLICY,
  CONTROL_RUNTIME_INIT_RPC_CANONICAL_CAPACITY,
  CONTROL_RUNTIME_INIT_LAYOUT_OVERFLOW,
  CONTROL_RUNTIME_INIT_STORAGE_TOO_SMALL,
  CONTROL_RUNTIME_INIT_STORAGE_NULL,
  CONTROL_RUNTIME_INIT_STORAGE_ALIGNMENT,
  CONTROL_RUNTIME_INIT_STORAGE_OVERLAP,
  CONTROL_RUNTIME_INIT_COMPONENT
};

typedef struct {
  control_runtime_init_issue_t issue;
  const char *field;
  size_t required;
  size_t provided;
} control_runtime_init_diagnostic_t;

const char *control_runtime_init_issue_str(control_runtime_init_issue_t issue);

/* Mechanical defaults use one FIFO/RPC slot, generation/operation ID one,
 * bounded encoded maxima, disabled roles, zero timeouts, and reject-new cache.
 * Override policy fields after this call. */
wl_err_t control_runtime_config_defaults(control_runtime_config_t *config);

wl_err_t control_runtime_config_enable_client(control_runtime_config_t *config);
wl_err_t control_runtime_config_enable_server(control_runtime_config_t *config);
control_runtime_storage_t control_runtime_default_storage_descriptor(control_runtime_default_storage_t *storage);
int control_runtime_requirements(const control_runtime_config_t *config, control_runtime_requirements_t *out_requirements);
/* Checked initialization reports the exact rejected field and capacity values. */
int control_runtime_init_checked(control_runtime_instance_t *instance, const control_runtime_config_t *config, const control_runtime_storage_t *storage, control_runtime_init_diagnostic_t *out_diagnostic);
int control_runtime_init(control_runtime_instance_t *instance, const control_runtime_config_t *config, const control_runtime_storage_t *storage);
/* With non-null ctx/event every RX outcome is consumed. Matching RPC TX
 * terminal events advance the runtime and reclaim the handle. Inspect
 * result.event_consumed before applying a fallback owner action. */
control_runtime_result_t control_runtime_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, control_runtime_t *runtime, wl_time_ms_t now_ms);

int control_joint_command_fifo_acquire(control_runtime_t *runtime, control_joint_command_fifo_view_t *out_view);
int control_joint_command_fifo_release(control_runtime_t *runtime, control_joint_command_fifo_view_t *view);

int control_arm_mit_command_latest_acquire(control_runtime_t *runtime, control_arm_mit_command_latest_view_t *out_view);
int control_arm_mit_command_latest_release(control_runtime_t *runtime, control_arm_mit_command_latest_view_t *view);

/* Observe a nonzero point-to-point peer before non-RPC traffic is handled. */
wl_rpc_err_t control_runtime_peer_observe(wl_ctx_t *ctx, control_runtime_t *runtime, uint64_t peer_session_id, wl_rpc_peer_observation_t *out_observation);
/* A reliable server request automatically observes its peer session before
 * dispatch. Take a changed observation to revoke product leases/non-RPC work. */
wl_rpc_err_t control_runtime_peer_observation_take(control_runtime_t *runtime, wl_rpc_peer_observation_t *out_observation);
/* Advance configured RPC deadlines without performing I/O. At most one
 * expired server identity is returned per call and remains pending until the
 * application completes, rejects, or abandons it. */
wl_rpc_err_t control_runtime_poll(control_runtime_t *runtime, wl_time_ms_t now_ms, control_runtime_poll_result_t *out_result);
/* Advance deadlines and submit at most one runtime-owned server response.
 * Link backpressure defers the same cached bytes for a later service call. */
wl_rpc_err_t control_runtime_service(wl_ctx_t *ctx, control_runtime_t *runtime, wl_time_ms_t now_ms, control_runtime_service_result_t *out_result);
/* Side-effect free. Zero is due; WL_RPC_NO_DEADLINE_MS means no deadline. */
wl_rpc_err_t control_runtime_get_deadline_hint(const control_runtime_t *runtime, wl_time_ms_t now_ms, wl_rpc_deadline_hint_t *out_hint);

/* Build pump hooks that dispatch events with the owner's time sample. RPC
 * profiles also service one queued response per pass and merge their deadline.
 * on_result may be null; result pointers are borrowed only for the callback. */
wl_err_t control_runtime_pump_init(control_runtime_pump_t *pump, control_runtime_t *runtime, control_runtime_result_fn on_result, void *user_data);
wl_pump_hooks_t control_runtime_pump_hooks(control_runtime_pump_t *pump);

/* Allocates, encodes, and submits atomically from the caller's view. A
 * present nonzero request operation ID is used exactly, allowing an explicit
 * retry to address the server's bounded replay cache; absent or zero selects an
 * automatically allocated ID. The const request is copied into runtime-owned
 * encode scratch before operation ID injection. A local encode/submit failure
 * releases the allocated RPC slot and returns operation_id zero. */
control_runtime_result_t control_home_client_start(wl_ctx_t *ctx, control_runtime_t *runtime, const home_request_t *request, uint32_t timeout_ms, wl_time_ms_t now_ms);
/* Nonblocking inspection returns generic metadata for this service. */
wl_rpc_err_t control_home_client_inspect(const control_runtime_t *runtime, uint32_t operation_id, wl_rpc_client_result_t *out_client);
/* Decode a retained response previously returned by client_inspect(). Borrowed
 * response fields remain valid only until client_release(). */
control_runtime_result_t control_home_client_decode(const wl_rpc_client_result_t *client, home_response_t *response);
wl_rpc_err_t control_home_client_release(control_runtime_t *runtime, uint32_t operation_id);

/* server_request is copied from the request callback and uniquely scopes this
 * execution generation. Completion copies the const response into runtime-owned
 * encode scratch before injecting operation ID/status. runtime_service() later
 * submits the cached response bytes. */
control_runtime_result_t control_home_server_complete(control_runtime_t *runtime, const wl_rpc_server_request_t *server_request, const home_response_t *response, wl_time_ms_t now_ms);
control_runtime_result_t control_home_server_reject(control_runtime_t *runtime, const wl_rpc_server_request_t *server_request, int32_t application_status, const home_response_t *response, wl_time_ms_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
