#ifndef WIRELINK_GENERATED_DEVICE_RUNTIME_H
#define WIRELINK_GENERATED_DEVICE_RUNTIME_H

#include "device_bindings.h"
#include <wirelink/pump.h>
#include <wirelink/endpoint.h>
#include <wirelink/allocator.h>
#include <wirelink/frame.h>
#include <string.h>
#include <wirelink/rpc.h>
#include <wirelink/rpc_sync.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_SCHEMA_IDENTITY UINT64_C(0x5A72C7FA9B968ADD)
#define DEVICE_BINDING_PROFILE_IDENTITY UINT64_C(0x0C01202B133FDA46)
#define DEVICE_BINDING_PROFILE_VERSION 1U
#define DEVICE_IDENTITY_ALGORITHM "fnv1a64-v1"

#define DEVICE_RUNTIME_CODEGEN_ABI_VERSION 32U

/* Generated capabilities, not application overrides. */
#define DEVICE_RUNTIME_HAS_RPC_CLIENT 1
#define DEVICE_RUNTIME_HAS_RPC_SERVER 1

#define DEVICE_RPC_REQUEST_FINGERPRINT_ALGORITHM "fnv1a64-canonical-request-v1"

typedef int32_t device_runtime_domain_t;
enum {
  DEVICE_RUNTIME_OK = 0,
  DEVICE_RUNTIME_NON_RX,
  DEVICE_RUNTIME_UNKNOWN_MESSAGE,
  DEVICE_RUNTIME_MISSING_ROUTE,
  DEVICE_RUNTIME_MISSING_SCRATCH,
  DEVICE_RUNTIME_DELIVERY_MISMATCH,
  DEVICE_RUNTIME_CODEC_ERROR,
  DEVICE_RUNTIME_STORAGE_ERROR,
  DEVICE_RUNTIME_RPC_ERROR,
  DEVICE_RUNTIME_CORE_ERROR,
  DEVICE_RUNTIME_APPLICATION_ERROR,
  DEVICE_RUNTIME_INVALID_ARGUMENT
};

typedef uint8_t device_runtime_detail_kind_t;
enum {
  DEVICE_RUNTIME_DETAIL_NONE = 0,
  DEVICE_RUNTIME_DETAIL_RETAINED = 1,
  DEVICE_RUNTIME_DETAIL_RPC = 2
};

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
} device_runtime_rpc_detail_t;

typedef union {
  device_runtime_rpc_detail_t rpc;
} device_runtime_detail_t;

/* Inspect detail only through the member selected by detail_kind. domain
 * classifies the outcome; zero-initialized unused detail fields retain their
 * corresponding success values. event_consumed is nonzero only when dispatch
 * released an RX event or reclaimed a terminal TX handle. */
typedef struct {
  device_runtime_domain_t domain;
  wl_event_type_t event_type;
  uint16_t message_id;
  device_runtime_detail_kind_t detail_kind;
  uint8_t event_consumed;
  device_runtime_detail_t detail;
} device_runtime_result_t;

/* Convenience helpers preserve the full diagnostic result. Detail accessors
 * return null unless detail_kind selects the requested member. Result strings
 * are diagnostic text and must not be parsed as a stable machine interface. */
static inline bool device_runtime_result_ok(const device_runtime_result_t *result) {
  return result != NULL && result->domain == DEVICE_RUNTIME_OK;
}

const char *device_runtime_result_str(const device_runtime_result_t *result);

static inline const device_runtime_rpc_detail_t *device_runtime_result_rpc_detail(const device_runtime_result_t *result) {
  return result != NULL && result->detail_kind == DEVICE_RUNTIME_DETAIL_RPC ? &result->detail.rpc : NULL;
}

#define DEVICE_RUNTIME_HAS_MANAGED_RPC 1
/* Copy the request token for deferred replies. Its private state is not
 * application data; it authorizes only this runtime and execution lifetime. */
typedef struct {
  struct {
    const void *owner;
    uint64_t incarnation;
    wl_rpc_server_request_t request;
  } private_state;
} device_configure_request_token_t;

/* Request fields are borrowed only for this callback. Nonzero return abandons
 * execution locally; send a rejection explicitly to notify the caller. */
typedef int32_t (*device_configure_rpc_request_handler_fn)(void *user_data,
    const configure_request_t *request, const device_configure_request_token_t *token,
    wl_delivery_t delivery);

#if CONFIGURE_REQUEST_HAS_VALUE && CONFIGURE_RESPONSE_HAS_VALUE
/* Ordinary immediate business handler: zero succeeds, nonzero rejects with
 * that application code. response is initialized before the callback. */
typedef int32_t (*device_configure_handler_fn)(void *user_data,
    const configure_request_value_t *request, configure_response_value_t *response);
/* A response exists only on SUCCESS and lives through this callback. Assign
 * *response to save an independent value. No call-slot release is required. */
typedef void (*device_configure_completion_fn)(void *user_data,
    const wl_rpc_completion_t *result, const configure_response_value_t *response);
#endif
typedef struct {
  configure_request_t *request_scratch;
  configure_response_t *response_scratch;
  device_configure_rpc_request_handler_fn request_handler;
  void *user_data;
#if CONFIGURE_REQUEST_HAS_VALUE && CONFIGURE_RESPONSE_HAS_VALUE
  device_configure_handler_fn value_handler;
  void *value_user_data;
  configure_request_value_t *request_value;
  configure_response_value_t *response_value;
#endif
} device_configure_rpc_t;
/* Copy the request token for deferred replies. Its private state is not
 * application data; it authorizes only this runtime and execution lifetime. */
typedef struct {
  struct {
    const void *owner;
    uint64_t incarnation;
    wl_rpc_server_request_t request;
  } private_state;
} device_get_info_request_token_t;

/* Request fields are borrowed only for this callback. Nonzero return abandons
 * execution locally; send a rejection explicitly to notify the caller. */
typedef int32_t (*device_get_info_rpc_request_handler_fn)(void *user_data,
    const info_request_t *request, const device_get_info_request_token_t *token,
    wl_delivery_t delivery);

#if INFO_REQUEST_HAS_VALUE && INFO_RESPONSE_HAS_VALUE
/* Ordinary immediate business handler: zero succeeds, nonzero rejects with
 * that application code. response is initialized before the callback. */
typedef int32_t (*device_get_info_handler_fn)(void *user_data,
    const info_request_value_t *request, info_response_value_t *response);
/* A response exists only on SUCCESS and lives through this callback. Assign
 * *response to save an independent value. No call-slot release is required. */
typedef void (*device_get_info_completion_fn)(void *user_data,
    const wl_rpc_completion_t *result, const info_response_value_t *response);
#endif
typedef struct {
  info_request_t *request_scratch;
  info_response_t *response_scratch;
  device_get_info_rpc_request_handler_fn request_handler;
  void *user_data;
#if INFO_REQUEST_HAS_VALUE && INFO_RESPONSE_HAS_VALUE
  device_get_info_handler_fn value_handler;
  void *value_user_data;
  info_request_value_t *request_value;
  info_response_value_t *response_value;
#endif
} device_get_info_rpc_t;
typedef struct {
  uint16_t client_timed_out;
  uint16_t server_pending_expired;
  uint16_t server_cache_expired;
  wl_rpc_server_request_t server_expired_request;
} device_runtime_poll_result_t;

typedef struct {
  device_runtime_poll_result_t deadlines;
  device_runtime_result_t response;
  uint16_t responses_submitted;
  uint16_t responses_deferred;
} device_runtime_service_result_t;

typedef struct {
  uint8_t _reserved;
  wl_rpc_client_t *rpc_client;
  wl_rpc_server_t *rpc_server;
  wl_rpc_peer_t rpc_peer;
  wl_rpc_peer_observation_t rpc_peer_observation;
  wl_tx_handle_t rpc_retiring_tx;
  uint64_t rpc_incarnation;
  wl_rpc_async_t *rpc_async;
  device_configure_rpc_t configure;
  device_get_info_rpc_t get_info;
} device_runtime_t;

typedef void (*device_runtime_result_fn)(void *user_data, const device_runtime_result_t *result);

typedef struct {
  device_runtime_t *runtime;
  void *user_data;
  device_runtime_result_fn on_result;
  wl_rpc_err_t last_service_result;
  device_runtime_service_result_t last_service;
} device_runtime_pump_t;

/* Static runtime assembly. requirements() validates every sizing field and
 * reports the exact caller-owned byte storage needed by init(). Configuration
 * and storage descriptors may be temporary; instance and storage must outlive
 * all runtime activity and must not be copied after successful initialization. */
typedef struct {
  uint8_t _reserved;
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
  device_configure_rpc_request_handler_fn configure_request_handler;
  void *configure_user_data;
  device_get_info_rpc_request_handler_fn get_info_request_handler;
  void *get_info_user_data;
} device_runtime_config_t;

#define DEVICE_RUNTIME_HAS_DEFAULT_STORAGE 1
typedef union {
  uint8_t byte;
  wl_rpc_client_slot_t rpc_client_slot;
  wl_rpc_server_pending_slot_t rpc_server_pending_slot;
  wl_rpc_server_cache_slot_t rpc_server_cache_slot;
} device_runtime_default_storage_alignment_t;

#if defined(__cplusplus)
#define DEVICE_RUNTIME_DEFAULT_STORAGE_ALIGNMENT alignof(device_runtime_default_storage_alignment_t)
#elif defined(_MSC_VER)
#define DEVICE_RUNTIME_DEFAULT_STORAGE_ALIGNMENT __alignof(device_runtime_default_storage_alignment_t)
#else
#define DEVICE_RUNTIME_DEFAULT_STORAGE_ALIGNMENT _Alignof(device_runtime_default_storage_alignment_t)
#endif
#define DEVICE_RUNTIME_DEFAULT_STORAGE_CAPACITY \
  (1U + \
   ((DEVICE_RUNTIME_DEFAULT_STORAGE_ALIGNMENT - 1U) + sizeof(wl_rpc_client_slot_t)) + \
   236U + \
   ((DEVICE_RUNTIME_DEFAULT_STORAGE_ALIGNMENT - 1U) + sizeof(wl_rpc_server_pending_slot_t)) + \
   ((DEVICE_RUNTIME_DEFAULT_STORAGE_ALIGNMENT - 1U) + sizeof(wl_rpc_server_cache_slot_t)) + \
   236U)

typedef union {
  device_runtime_default_storage_alignment_t alignment;
  uint8_t bytes[DEVICE_RUNTIME_DEFAULT_STORAGE_CAPACITY];
} device_runtime_default_storage_t;

typedef union { configure_request_t request; configure_response_t response; } device_runtime_configure_decode_detail_t;
typedef union { info_request_t request; info_response_t response; } device_runtime_get_info_decode_detail_t;
typedef struct {
  size_t storage_size;
  size_t storage_alignment;
} device_runtime_requirements_t;

typedef struct {
  void *data;
  size_t size;
} device_runtime_storage_t;

typedef struct {
  device_runtime_t runtime;
  wl_rpc_client_t rpc_client;
  wl_rpc_server_t rpc_server;
  /* One dispatch at a time: bounded services share decode scratch.
   * Views are callback-scoped; deferred work must copy its input. */
  union {
    device_runtime_configure_decode_detail_t configure_scratch;
    device_runtime_get_info_decode_detail_t get_info_scratch;
  };
} device_runtime_instance_t;

typedef int32_t device_runtime_init_issue_t;
enum {
  DEVICE_RUNTIME_INIT_OK = 0,
  DEVICE_RUNTIME_INIT_NULL_ARGUMENT,
  DEVICE_RUNTIME_INIT_ROLE_ENABLE,
  DEVICE_RUNTIME_INIT_RETAINED_CAPACITY,
  DEVICE_RUNTIME_INIT_RPC_CLIENT_CAPACITY,
  DEVICE_RUNTIME_INIT_RPC_SERVER_CAPACITY,
  DEVICE_RUNTIME_INIT_RPC_TIMEOUT,
  DEVICE_RUNTIME_INIT_RPC_CACHE_POLICY,
  DEVICE_RUNTIME_INIT_LAYOUT_OVERFLOW,
  DEVICE_RUNTIME_INIT_STORAGE_TOO_SMALL,
  DEVICE_RUNTIME_INIT_STORAGE_NULL,
  DEVICE_RUNTIME_INIT_STORAGE_ALIGNMENT,
  DEVICE_RUNTIME_INIT_STORAGE_OVERLAP,
  DEVICE_RUNTIME_INIT_COMPONENT
};

typedef struct {
  device_runtime_init_issue_t issue;
  const char *field;
  size_t required;
  size_t provided;
} device_runtime_init_diagnostic_t;

const char *device_runtime_init_issue_str(device_runtime_init_issue_t issue);

/* Mechanical defaults use one FIFO/RPC slot, generation/operation ID one,
 * bounded encoded maxima, disabled roles, zero timeouts, and reject-new cache.
 * Override policy fields after this call. */
wl_err_t device_runtime_config_defaults(device_runtime_config_t *config);

wl_err_t device_runtime_config_enable_client(device_runtime_config_t *config);
wl_err_t device_runtime_config_enable_server(device_runtime_config_t *config);
device_runtime_storage_t device_runtime_default_storage_descriptor(device_runtime_default_storage_t *storage);
int device_runtime_requirements(const device_runtime_config_t *config, device_runtime_requirements_t *out_requirements);
/* Checked initialization reports the exact rejected field and capacity values. */
int device_runtime_init_checked(device_runtime_instance_t *instance, const device_runtime_config_t *config, const device_runtime_storage_t *storage, device_runtime_init_diagnostic_t *out_diagnostic);
int device_runtime_init(device_runtime_instance_t *instance, const device_runtime_config_t *config, const device_runtime_storage_t *storage);
/* With non-null ctx/event every RX outcome is consumed. Matching RPC TX
 * terminal events advance the runtime and reclaim the handle. Inspect
 * result.event_consumed before applying a fallback owner action. */
device_runtime_result_t device_runtime_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, device_runtime_t *runtime, wl_time_ms_t now_ms);

/* Observe a nonzero point-to-point peer before non-RPC traffic is handled. */
wl_rpc_err_t device_runtime_peer_observe(wl_ctx_t *ctx, device_runtime_t *runtime, uint64_t peer_session_id, wl_rpc_peer_observation_t *out_observation);
/* A reliable server request automatically observes its peer session before
 * dispatch. Take a changed observation to revoke product leases/non-RPC work. */
wl_rpc_err_t device_runtime_peer_observation_take(device_runtime_t *runtime, wl_rpc_peer_observation_t *out_observation);
/* Advance configured RPC deadlines without performing I/O. At most one
 * expired server identity is returned per call and remains pending until the
 * application completes, rejects, or abandons it. */
wl_rpc_err_t device_runtime_poll(device_runtime_t *runtime, wl_time_ms_t now_ms, device_runtime_poll_result_t *out_result);
/* Advance deadlines and submit at most one runtime-owned server response.
 * Link backpressure defers the same cached bytes for a later service call. */
wl_rpc_err_t device_runtime_service(wl_ctx_t *ctx, device_runtime_t *runtime, wl_time_ms_t now_ms, device_runtime_service_result_t *out_result);
/* Side-effect free. Zero is due; WL_RPC_NO_DEADLINE_MS means no deadline. */
wl_rpc_err_t device_runtime_get_deadline_hint(const device_runtime_t *runtime, wl_time_ms_t now_ms, wl_rpc_deadline_hint_t *out_hint);

/* Build pump hooks that dispatch events with the owner's time sample. RPC
 * profiles also service one queued response per pass and merge their deadline.
 * on_result may be null; result pointers are borrowed only for the callback. */
wl_err_t device_runtime_pump_init(device_runtime_pump_t *pump, device_runtime_t *runtime, device_runtime_result_fn on_result, void *user_data);
wl_pump_hooks_t device_runtime_pump_hooks(device_runtime_pump_t *pump);

/* Advanced runtime integration. The default endpoint call API below hides
 * numeric correlation IDs. Managed metadata uses a separate 20-byte header. */
device_runtime_result_t device_configure_client_start(wl_ctx_t *ctx,
    device_runtime_t *runtime, const configure_request_t *request, uint32_t timeout_ms,
    wl_time_ms_t now_ms);
wl_rpc_err_t device_configure_client_inspect(const device_runtime_t *runtime,
    uint32_t operation_id, wl_rpc_client_result_t *out_client);
/* Response fields which borrow bytes remain valid until call release. */
device_runtime_result_t device_configure_client_decode(const wl_rpc_client_result_t *client,
    configure_response_t *response);
wl_rpc_err_t device_configure_client_release(device_runtime_t *runtime, uint32_t operation_id);
device_runtime_result_t device_configure_server_complete(device_runtime_t *runtime,
    const device_configure_request_token_t *token, const configure_response_t *response,
    wl_time_ms_t now_ms);
/* A nonzero business status is sent without a response body. */
device_runtime_result_t device_configure_server_reject(device_runtime_t *runtime,
    const device_configure_request_token_t *token, int32_t application_status,
    wl_time_ms_t now_ms);

/* Advanced scheduling/diagnostics: inspect a token from this runtime incarnation.
 * Not a liveness test: complete/reject still validate the pending reservation.
 * Does not grant permission to reconstruct tokens or extend their lifetime. */
wl_rpc_err_t device_configure_request_inspect(device_runtime_t *runtime,
    const device_configure_request_token_t *token, wl_rpc_server_request_t *out_request);
#if CONFIGURE_REQUEST_HAS_VALUE && CONFIGURE_RESPONSE_HAS_VALUE
/* Generator-owned snapshot/response helpers. Prefer endpoint operations. */
wl_err_t device_configure_encode_submission(const void *request, uint32_t operation_id, uint64_t session,
    uint8_t *out, size_t capacity, size_t *length);
device_runtime_result_t device_configure_server_complete_value(device_runtime_t *runtime,
    const device_configure_request_token_t *token, const configure_response_value_t *response,
    wl_time_ms_t now_ms);
#endif
/* Advanced runtime integration. The default endpoint call API below hides
 * numeric correlation IDs. Managed metadata uses a separate 20-byte header. */
device_runtime_result_t device_get_info_client_start(wl_ctx_t *ctx,
    device_runtime_t *runtime, const info_request_t *request, uint32_t timeout_ms,
    wl_time_ms_t now_ms);
wl_rpc_err_t device_get_info_client_inspect(const device_runtime_t *runtime,
    uint32_t operation_id, wl_rpc_client_result_t *out_client);
/* Response fields which borrow bytes remain valid until call release. */
device_runtime_result_t device_get_info_client_decode(const wl_rpc_client_result_t *client,
    info_response_t *response);
wl_rpc_err_t device_get_info_client_release(device_runtime_t *runtime, uint32_t operation_id);
device_runtime_result_t device_get_info_server_complete(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, const info_response_t *response,
    wl_time_ms_t now_ms);
/* A nonzero business status is sent without a response body. */
device_runtime_result_t device_get_info_server_reject(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, int32_t application_status,
    wl_time_ms_t now_ms);

/* Advanced scheduling/diagnostics: inspect a token from this runtime incarnation.
 * Not a liveness test: complete/reject still validate the pending reservation.
 * Does not grant permission to reconstruct tokens or extend their lifetime. */
wl_rpc_err_t device_get_info_request_inspect(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, wl_rpc_server_request_t *out_request);
#if INFO_REQUEST_HAS_VALUE && INFO_RESPONSE_HAS_VALUE
/* Generator-owned snapshot/response helpers. Prefer endpoint operations. */
wl_err_t device_get_info_encode_submission(const void *request, uint32_t operation_id, uint64_t session,
    uint8_t *out, size_t capacity, size_t *length);
device_runtime_result_t device_get_info_server_complete_value(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, const info_response_value_t *response,
    wl_time_ms_t now_ms);
#endif
/* Default endpoint assembly. Members named private_state are not application
 * API. Zero-initialize once, keep at a stable address, close before reuse.
 * All profile-selected messages must have finite one-frame bounds. */
#define DEVICE_HAS_DEFAULT_ENDPOINT 1
#define DEVICE_ENDPOINT_MAX_PAYLOAD 236U
/* Layout is fixed by the local profile; CRC32C bounds also cover smaller CRCs. */
#define DEVICE_ENDPOINT_RAW_CAPACITY (DEVICE_ENDPOINT_MAX_PAYLOAD + WL_FRAME_HEADER_SIZE + WL_FRAME_MAX_CRC)
#define DEVICE_ENDPOINT_UNIT_CAPACITY (DEVICE_ENDPOINT_RAW_CAPACITY + DEVICE_ENDPOINT_RAW_CAPACITY / 254U + 2U)
#define DEVICE_ENDPOINT_CONTROL_CAPACITY (WL_FRAME_HEADER_SIZE + WL_FRAME_MAX_CRC + 2U)
#ifndef DEVICE_ENDPOINT_RX_FIFO_CAPACITY
#define DEVICE_ENDPOINT_RX_FIFO_CAPACITY DEVICE_ENDPOINT_UNIT_CAPACITY
#endif
/* Set consistently for every TU using this endpoint; no runtime allocation. */
#ifndef DEVICE_ENDPOINT_RPC_CAPACITY
#define DEVICE_ENDPOINT_RPC_CAPACITY 4U
#endif
#if DEVICE_ENDPOINT_RPC_CAPACITY < 1 || DEVICE_ENDPOINT_RPC_CAPACITY > 65535
#error "endpoint RPC capacity must be 1..65535"
#endif
#define DEVICE_ENDPOINT_REQUEST_CAPACITY 230U
#define DEVICE_ENDPOINT_RUNTIME_CAPACITY (DEVICE_RUNTIME_DEFAULT_STORAGE_CAPACITY + (DEVICE_ENDPOINT_RPC_CAPACITY - 1U) * (sizeof(wl_rpc_client_slot_t) + 236U + sizeof(wl_rpc_server_pending_slot_t) + sizeof(wl_rpc_server_cache_slot_t) + 236U))

typedef struct {
  wl_config_t link;
  wl_environment_t environment;
  /* Expert policy/storage overrides; ordinary applications use defaults. */
  device_runtime_config_t advanced;
  device_configure_handler_fn on_configure;
  void *configure_user_data; /* NULL inherits config.user_data. */
  device_get_info_handler_fn on_get_info;
  void *get_info_user_data; /* NULL inherits config.user_data. */


  wl_rpc_response_observer_fn on_response_terminal;
  size_t event_budget;
  device_runtime_result_fn on_result;
  /* Shared context for on_result and ordinary on_<rpc> handlers. Advanced
   * deferred handlers and per-call completion contexts remain explicit. */
  void *user_data;
} device_endpoint_config_t;

typedef struct {
  struct {
    wl_endpoint_t owner;
    wl_allocator_t allocator; /* Zero for caller-owned static storage. */
    bool stepping;
    bool closing;
    uint64_t previous_session;
    device_runtime_instance_t instance;
    union {
      device_runtime_default_storage_alignment_t alignment;
      uint8_t bytes[DEVICE_ENDPOINT_RUNTIME_CAPACITY];
    } arena;
    device_runtime_pump_t pump;
    device_runtime_result_t result;
    device_runtime_result_fn on_result;
    void *user_data;
    size_t event_budget;
    uint64_t incarnation;
    bool sync_waiting;
    wl_rpc_async_t async;
    wl_rpc_async_slot_t submissions[DEVICE_ENDPOINT_RPC_CAPACITY];
    uint8_t requests[DEVICE_ENDPOINT_RPC_CAPACITY][DEVICE_ENDPOINT_REQUEST_CAPACITY];
    wl_rpc_completion_t completion;
    union {
      struct { configure_request_value_t request; configure_response_value_t response; } configure;
      struct { info_request_value_t request; info_response_value_t response; } get_info;
    } values;
    uint8_t tx_payload[DEVICE_ENDPOINT_MAX_PAYLOAD];
    uint8_t tx_unit[DEVICE_ENDPOINT_UNIT_CAPACITY];
    uint8_t control_unit[DEVICE_ENDPOINT_CONTROL_CAPACITY];
    uint8_t rx_fallback[DEVICE_ENDPOINT_UNIT_CAPACITY];
    uint8_t rx_fifo[DEVICE_ENDPOINT_RX_FIFO_CAPACITY];
  } private_state;
} device_endpoint_t;

#if defined(__cplusplus)
#define DEVICE_ENDPOINT_ALIGNMENT alignof(device_endpoint_t)
#elif defined(_MSC_VER)
#define DEVICE_ENDPOINT_ALIGNMENT __alignof(device_endpoint_t)
#else
#define DEVICE_ENDPOINT_ALIGNMENT _Alignof(device_endpoint_t)
#endif

/* Config descriptors can be temporary; callbacks/user_data must outlive use.
 * Ordinary client capability and registered server handlers are assembled by init. */
static inline wl_err_t device_endpoint_config_defaults(
    device_endpoint_config_t *config, wl_environment_t environment) {
  int result;
  if (config == NULL) return WL_ERR_INVALID_ARG;
  memset(config, 0, sizeof(*config));
  config->link.max_payload_len = DEVICE_ENDPOINT_MAX_PAYLOAD;
  config->link.envelope = WL_ENVELOPE_NATIVE_PACKET;
  config->link.integrity = WL_INTEGRITY_CRC32C;
  config->environment = environment;
  config->link.ack_timeout_ms = 100U;
  config->link.max_retries = 4U;
  config->event_budget = 16U;
  result = device_runtime_config_defaults(&config->advanced);
  if (result != WL_OK) return result;
  config->advanced.rpc_client_enabled = 1U;
  config->advanced.rpc_client_slot_count = DEVICE_ENDPOINT_RPC_CAPACITY;
  config->advanced.rpc_server_pending_slot_count = DEVICE_ENDPOINT_RPC_CAPACITY;
  config->advanced.rpc_server_cache_slot_count = DEVICE_ENDPOINT_RPC_CAPACITY;
  config->advanced.rpc_server_pending_timeout_ms = 1000U;
  config->advanced.rpc_server_cache_ttl_ms = 10000U;
  config->advanced.rpc_server_cache_policy = WL_RPC_CACHE_EVICT_OLDEST;
  return WL_OK;
}

static inline wl_endpoint_t *device_endpoint_handle(device_endpoint_t *endpoint) {
  return endpoint != NULL ? &endpoint->private_state.owner : NULL;
}

/* Advanced integration only: do not also dispatch/take events consumed by step. */
static inline device_runtime_t *device_endpoint_runtime(device_endpoint_t *endpoint) {
  return endpoint != NULL && wl_endpoint_link(device_endpoint_handle(endpoint)) != NULL
      ? &endpoint->private_state.instance.runtime : NULL;
}

static inline void device_endpoint_record(void *context,
                                      const device_runtime_result_t *result) {
  device_endpoint_t *endpoint = (device_endpoint_t *)context;
  device_runtime_result_t terminal;
  if (result->domain == DEVICE_RUNTIME_NON_RX) {
    if (result->event_type != WL_EVT_TX_TIMEOUT && result->event_type != WL_EVT_TX_FAILED)
      return; /* Normal transport completion is not an application failure. */
    terminal = *result;
    terminal.domain = DEVICE_RUNTIME_CORE_ERROR;
    result = &terminal;
  }
#if DEVICE_RUNTIME_HAS_MANAGED_RPC
  /* Cached replies retain their reservation and retry ordinary link pressure.
   * Per-call failure/deadline remains observable through completion. */
  if (result->domain == DEVICE_RUNTIME_CORE_ERROR && result->detail_kind == DEVICE_RUNTIME_DETAIL_RPC &&
      (result->detail.rpc.core_result == WL_ERR_BUSY ||
       result->detail.rpc.core_result == WL_ERR_WOULD_BLOCK ||
       result->detail.rpc.core_result == WL_ERR_QUEUE_FULL ||
       result->detail.rpc.core_result == WL_ERR_NO_SPACE)) return;
#endif
  /* Retain the first failure even if later events in the same pass succeed. */
  if (device_runtime_result_ok(&endpoint->private_state.result))
    endpoint->private_state.result = *result;
  if (endpoint->private_state.on_result != NULL)
    endpoint->private_state.on_result(endpoint->private_state.user_data, result);
}

static inline wl_err_t device_endpoint_init_config(
    device_endpoint_t *endpoint, const device_endpoint_config_t *config) {
  wl_storage_t link_storage;
  device_runtime_storage_t storage;
  wl_pump_hooks_t hooks;
  device_runtime_config_t runtime_config;
  wl_config_t link_config;
  int result;
  if (endpoint == NULL || config == NULL || config->event_budget == 0U)
    return WL_ERR_INVALID_ARG;
  if (endpoint->private_state.stepping || endpoint->private_state.closing)
    return WL_ERR_REENTRANT;
  if (wl_endpoint_link(device_endpoint_handle(endpoint)) != NULL)
    return WL_ERR_INVALID_STATE;
  if (config->environment.clock.now_ms == NULL || config->link.session_id != 0U)
    return WL_ERR_INVALID_ARG;

  runtime_config = config->advanced;

  if (config->on_configure != NULL && runtime_config.configure_request_handler != NULL) return WL_ERR_INVALID_ARG;
  if (config->on_configure != NULL || runtime_config.configure_request_handler != NULL) runtime_config.rpc_server_enabled = 1U;
  if (config->on_get_info != NULL && runtime_config.get_info_request_handler != NULL) return WL_ERR_INVALID_ARG;
  if (config->on_get_info != NULL || runtime_config.get_info_request_handler != NULL) runtime_config.rpc_server_enabled = 1U;
  if (endpoint->private_state.closing) return WL_ERR_REENTRANT;
  if ((runtime_config.rpc_client_enabled && runtime_config.rpc_client_slot_count > DEVICE_ENDPOINT_RPC_CAPACITY) ||
      (runtime_config.rpc_server_enabled && (runtime_config.rpc_server_pending_slot_count > DEVICE_ENDPOINT_RPC_CAPACITY ||
      runtime_config.rpc_server_cache_slot_count > DEVICE_ENDPOINT_RPC_CAPACITY))) return WL_ERR_INVALID_ARG;

  link_config = config->link;
  result = wl_session_next(config->environment.session,
      endpoint->private_state.previous_session, &link_config.session_id);
  if (result != WL_OK) return result;
  if (endpoint->private_state.incarnation == UINT64_MAX) return WL_ERR_INVALID_STATE;
  ++endpoint->private_state.incarnation;
  memset(&link_storage, 0, sizeof(link_storage));
  link_storage.tx_payload = endpoint->private_state.tx_payload;
  link_storage.tx_payload_size = sizeof(endpoint->private_state.tx_payload);
  link_storage.tx_unit = endpoint->private_state.tx_unit;
  link_storage.tx_unit_size = sizeof(endpoint->private_state.tx_unit);
  link_storage.control_unit = endpoint->private_state.control_unit;
  link_storage.control_unit_size = sizeof(endpoint->private_state.control_unit);
  link_storage.rx_fallback = endpoint->private_state.rx_fallback;
  link_storage.rx_fallback_size = sizeof(endpoint->private_state.rx_fallback);
  link_storage.rx_fifo = endpoint->private_state.rx_fifo;
  link_storage.rx_fifo_size = sizeof(endpoint->private_state.rx_fifo);
  storage.data = endpoint->private_state.arena.bytes;
  storage.size = sizeof(endpoint->private_state.arena.bytes);
  result = device_runtime_init(&endpoint->private_state.instance, &runtime_config, &storage);
  if (result != WL_OK) return result;
  endpoint->private_state.instance.runtime.rpc_incarnation = endpoint->private_state.incarnation;
  endpoint->private_state.instance.runtime.configure.value_handler = config->on_configure;
  endpoint->private_state.instance.runtime.configure.value_user_data = config->configure_user_data != NULL ? config->configure_user_data : config->user_data;
  endpoint->private_state.instance.runtime.configure.request_value = &endpoint->private_state.values.configure.request;
  endpoint->private_state.instance.runtime.configure.response_value = &endpoint->private_state.values.configure.response;
  endpoint->private_state.instance.runtime.get_info.value_handler = config->on_get_info;
  endpoint->private_state.instance.runtime.get_info.value_user_data = config->get_info_user_data != NULL ? config->get_info_user_data : config->user_data;
  endpoint->private_state.instance.runtime.get_info.request_value = &endpoint->private_state.values.get_info.request;
  endpoint->private_state.instance.runtime.get_info.response_value = &endpoint->private_state.values.get_info.response;

  if (config->on_response_terminal != NULL) {
    if (endpoint->private_state.instance.runtime.rpc_server == NULL) return WL_ERR_INVALID_ARG;
    if (wl_rpc_server_set_response_observer(endpoint->private_state.instance.runtime.rpc_server, config->on_response_terminal, config->user_data) != WL_RPC_OK) return WL_ERR_INVALID_STATE;
  }
  result = device_runtime_pump_init(&endpoint->private_state.pump,
      &endpoint->private_state.instance.runtime, device_endpoint_record, endpoint);
  if (result != WL_OK) return result;
  hooks = device_runtime_pump_hooks(&endpoint->private_state.pump);
  result = wl_endpoint_init(&endpoint->private_state.owner, &link_config,
                            &link_storage, &config->environment.clock, &hooks);
  if (result != WL_OK) return result;
  endpoint->private_state.previous_session = link_config.session_id;
  if (runtime_config.rpc_client_enabled) {
    result = wl_rpc_async_init(&endpoint->private_state.async,
        wl_endpoint_link(&endpoint->private_state.owner), endpoint->private_state.instance.runtime.rpc_client,
        endpoint->private_state.submissions, runtime_config.rpc_client_slot_count,
        endpoint->private_state.requests[0], sizeof(endpoint->private_state.requests),
        DEVICE_ENDPOINT_REQUEST_CAPACITY, endpoint->private_state.incarnation);
    if (result != WL_OK) { wl_endpoint_close(&endpoint->private_state.owner); return result; }
    endpoint->private_state.instance.runtime.rpc_async = &endpoint->private_state.async;
  }
  endpoint->private_state.on_result = config->on_result;
  endpoint->private_state.user_data = config->user_data;
  endpoint->private_state.event_budget = config->event_budget;
  memset(&endpoint->private_state.result, 0, sizeof(endpoint->private_state.result));
  return WL_OK;
}

static inline wl_err_t device_endpoint_init(device_endpoint_t *endpoint,
                                        wl_environment_t environment) {
  device_endpoint_config_t config;
  int result = device_endpoint_config_defaults(&config, environment);
  return result == WL_OK ? device_endpoint_init_config(endpoint, &config) : result;
}

/* One bounded owner pass: service transport, dispatch and release events,
 * advance RPC work. NO_DATA/backpressure during transport service is normal.
 * Inspect endpoint_result/last_step for details when this returns an error. */
static inline wl_err_t device_endpoint_step(device_endpoint_t *endpoint) {
  int result;
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_state.stepping || endpoint->private_state.closing) return WL_ERR_REENTRANT;
  endpoint->private_state.stepping = true;
  memset(&endpoint->private_state.result, 0, sizeof(endpoint->private_state.result));
  result = wl_endpoint_step(&endpoint->private_state.owner,
                             endpoint->private_state.event_budget);
  endpoint->private_state.stepping = false;
  if (result != WL_OK) return result;
  if (endpoint->private_state.pump.last_service_result != WL_RPC_OK) {
    if (device_runtime_result_ok(&endpoint->private_state.result)) {
      endpoint->private_state.result.domain = DEVICE_RUNTIME_RPC_ERROR;
      endpoint->private_state.result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
      endpoint->private_state.result.detail.rpc.rpc_result = endpoint->private_state.pump.last_service_result;
    }
    return WL_ERR_INVALID_STATE;
  }
  return device_runtime_result_ok(&endpoint->private_state.result)
      ? WL_OK : WL_ERR_INVALID_STATE;
}

static inline const device_runtime_result_t *device_endpoint_result(const device_endpoint_t *endpoint) {
  return endpoint != NULL ? &endpoint->private_state.result : NULL;
}

static inline wl_err_t device_endpoint_close(device_endpoint_t *endpoint) {
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_state.stepping || endpoint->private_state.closing) return WL_ERR_REENTRANT;
  endpoint->private_state.closing = true;
  wl_endpoint_close(device_endpoint_handle(endpoint));
  {
    int error = wl_rpc_async_close(&endpoint->private_state.async);
    endpoint->private_state.closing = false;
    if (error != WL_OK) return error;
  }
  endpoint->private_state.closing = false;
  return WL_OK;
}

static inline wl_err_t device_endpoint_driver_step(void *context) {
  return device_endpoint_step((device_endpoint_t *)context);
}
static inline wl_err_t device_endpoint_driver_close(void *context) {
  return device_endpoint_close((device_endpoint_t *)context);
}
/* Setup-only platform integration; ordinary code does not drive both objects. */
static inline wl_endpoint_driver_t device_endpoint_driver(device_endpoint_t *endpoint) {
  wl_endpoint_driver_t driver;
  driver.endpoint = device_endpoint_handle(endpoint);
  driver.context = endpoint;
  driver.step = device_endpoint_driver_step;
  driver.close = device_endpoint_driver_close;
  driver.readiness_complete = 1U;
  return driver;
}

/* Optional creation: one allocation for the complete endpoint and all protocol
 * storage. *out must be NULL; failure leaves it unchanged. No global allocator
 * or fallback heap. Configure transport/waiting after successful creation. */
static inline wl_err_t device_endpoint_create(device_endpoint_t **out,
    const device_endpoint_config_t *config, const wl_allocator_t *allocator) {
  device_endpoint_t *endpoint;
  wl_allocator_t storage;
  int error;
  if (out == NULL || *out != NULL || config == NULL || allocator == NULL ||
      allocator->allocate == NULL || allocator->deallocate == NULL) return WL_ERR_INVALID_ARG;
  storage = *allocator;
  endpoint = (device_endpoint_t *)storage.allocate(storage.context,
      sizeof(*endpoint), DEVICE_ENDPOINT_ALIGNMENT);
  if (endpoint == NULL) return WL_ERR_NO_MEM;
  if ((uintptr_t)endpoint % DEVICE_ENDPOINT_ALIGNMENT != 0U) {
    storage.deallocate(storage.context, endpoint, sizeof(*endpoint), DEVICE_ENDPOINT_ALIGNMENT);
    return WL_ERR_INVALID_ARG;
  }
  memset(endpoint, 0, sizeof(*endpoint));
  error = device_endpoint_init_config(endpoint, config);
  if (error != WL_OK) {
    (void)device_endpoint_close(endpoint);
    storage.deallocate(storage.context, endpoint, sizeof(*endpoint), DEVICE_ENDPOINT_ALIGNMENT);
    return error;
  }
  endpoint->private_state.allocator = storage;
  *out = endpoint;
  return WL_OK;
}

/* Owner safe point, after background executor stop and all caller joins.
 * Close/quiesce and finish callbacks before freeing. Reentrant failure leaves
 * the pointer alive. Static storage must use close, not destroy. NULL is a no-op.
 * Copies of this owning pointer are invalid after successful destruction. */
static inline wl_err_t device_endpoint_destroy(device_endpoint_t **owner) {
  device_endpoint_t *endpoint;
  wl_allocator_t storage;
  int error;
  if (owner == NULL) return WL_ERR_INVALID_ARG;
  endpoint = *owner;
  if (endpoint == NULL) return WL_OK;
  storage = endpoint->private_state.allocator;
  if (storage.deallocate == NULL) return WL_ERR_INVALID_STATE;
  error = device_endpoint_close(endpoint);
  if (error != WL_OK) return error;
  *owner = NULL;
  storage.deallocate(storage.context, endpoint, sizeof(*endpoint), DEVICE_ENDPOINT_ALIGNMENT);
  return WL_OK;
}

/* Optional cancellation; completion still arrives once, with no release. */
static inline wl_err_t device_endpoint_cancel(device_endpoint_t *endpoint, const wl_rpc_call_t *call) {
  if (endpoint == NULL || call == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_state.closing || wl_endpoint_link(device_endpoint_handle(endpoint)) == NULL) return WL_ERR_NOT_INITIALIZED;
  return wl_rpc_async_cancel(&endpoint->private_state.async, call);
}
/* Generator internals: all services reuse one bounded completion scratch. */
static inline void device_endpoint_configure_prepare(void *context,
    const wl_rpc_client_result_t *client) {
  device_endpoint_t *endpoint = (device_endpoint_t *)context;
  wl_rpc_completion_t *result = &endpoint->private_state.completion;
  wl_rpc_async_completion(client, result);
  if (result->status == WL_RPC_SUCCESS) {
    if (client->response_length < 20U) {
      result->status = WL_RPC_FAILED;
      result->runtime_error = WL_RPC_ERR_MALFORMED_METADATA;
      return;
    }
    result->codec_error = configure_response_value_decode(client->response_data + 20U,
        client->response_length - 20U, &endpoint->private_state.values.configure.response);
    if (result->codec_error != WL_CODEC_OK) result->status = WL_RPC_FAILED;
  }
}
static inline void device_endpoint_configure_notify(void *context,
    wl_rpc_callback_t callback, void *user_data) {
  device_endpoint_t *endpoint = (device_endpoint_t *)context;
  ((device_configure_completion_fn)callback)(user_data, &endpoint->private_state.completion,
      endpoint->private_state.completion.status == WL_RPC_SUCCESS
          ? &endpoint->private_state.values.configure.response : NULL);
}

/* Snapshot and accept a call; callback is delivered by step or orderly close.
 * out_call is optional cancellation authority. Successful completion owns its
 * fields; copy *response during the callback to save it. Never release a slot.
 * No callback on failed admission; BUSY means local bounded capacity is full. */
static inline wl_err_t device_endpoint_configure_submit_at(device_endpoint_t *endpoint,
    const configure_request_value_t *request, uint32_t timeout_ms, wl_time_ms_t now_ms,
    device_configure_completion_fn callback, void *user_data, wl_rpc_call_t *out_call) {
  wl_rpc_async_observer_t observer;
  if (endpoint == NULL || request == NULL || callback == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_state.closing) return WL_ERR_NOT_INITIALIZED;
  observer.prepare = device_endpoint_configure_prepare;
  observer.notify = device_endpoint_configure_notify;
  observer.context = endpoint;
  observer.callback = (wl_rpc_callback_t)callback;
  observer.user_data = user_data;
  return wl_rpc_async_submit(&endpoint->private_state.async, 5U, 6U,
      WL_DELIVERY_RELIABLE, timeout_ms, now_ms, device_configure_encode_submission, request,
      &observer, out_call);
}

static inline wl_err_t device_endpoint_configure_async(device_endpoint_t *endpoint,
    const configure_request_value_t *request, uint32_t timeout_ms,
    device_configure_completion_fn callback, void *user_data, wl_rpc_call_t *out_call) {
  wl_time_ms_t now_ms;
  int error;
  if (endpoint == NULL || request == NULL || callback == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_state.closing) return WL_ERR_NOT_INITIALIZED;
  error = wl_endpoint_now(device_endpoint_handle(endpoint), &now_ms);
  if (error != WL_OK) return error;
  return device_endpoint_configure_submit_at(endpoint, request, timeout_ms, now_ms,
      callback, user_data, out_call);
}

/* Internal stack completion; no large response temporary or borrowed fields. */
typedef struct {
  bool done;
  wl_rpc_completion_t result;
  configure_response_value_t *response;
  device_endpoint_t *endpoint;
  const configure_request_value_t *request;
  wl_rpc_sync_notify_fn notify;
  void *notify_context;
} device_configure_sync_state_t;

static inline void device_endpoint_configure_sync_done(void *context,
    const wl_rpc_completion_t *result, const configure_response_value_t *response) {
  device_configure_sync_state_t *state = (device_configure_sync_state_t *)context;
  state->result = *result;
  if (response != NULL) *state->response = *response;
  state->done = true;
  if (state->notify != NULL) state->notify(state->notify_context, &state->result);
}

static inline wl_err_t device_endpoint_configure_proxy_submit(void *context,
    wl_time_ms_t deadline, wl_rpc_sync_notify_fn notify, void *notify_context,
    wl_rpc_call_t *call) {
  device_configure_sync_state_t *state = (device_configure_sync_state_t *)context;
  wl_time_ms_t now_ms;
  int error = wl_endpoint_now(device_endpoint_handle(state->endpoint), &now_ms);
  if (error != WL_OK) return error;
  const uint32_t remaining = deadline - now_ms;
  if (remaining == 0U || remaining > INT32_MAX) return WL_ERR_TIMEOUT;
  state->notify = notify;
  state->notify_context = notify_context;
  return device_endpoint_configure_submit_at(state->endpoint, state->request, remaining, now_ms,
      device_endpoint_configure_sync_done, state, call);
}

/* Platform call: use the installed waiter on the owner thread, or the bound
 * executor's proxy from business threads. Reuses async deadlines/completion;
 * never call from the same owner's callbacks. Response changes only on SUCCESS. Admission
 * and platform failures use local_error, not business rejection/transport_error.
 * On return there is no remaining callback referring to this function's stack. */
static inline wl_rpc_completion_t device_endpoint_configure_sync(device_endpoint_t *endpoint,
    const configure_request_value_t *request, configure_response_value_t *response, uint32_t timeout_ms) {
  device_configure_sync_state_t state;
  wl_rpc_call_t call;
  wl_waiter_t waiter;
  const wl_waiter_t *platform;
  const wl_rpc_executor_t *executor;
  wl_err_t error;
  memset(&state, 0, sizeof(state));
  state.result.status = WL_RPC_FAILED;
  state.response = response;
  state.endpoint = endpoint;
  state.request = request;
  if (endpoint == NULL || request == NULL || response == NULL) {
    state.result.local_error = WL_ERR_INVALID_ARG;
    return state.result;
  }
  /* The binding is immutable while callers run. Do not inspect mutable owner
   * state on a proxy caller thread, including during executor shutdown. */
  executor = wl_endpoint_rpc_executor(device_endpoint_handle(endpoint));
  if (executor != NULL) {
    const wl_rpc_sync_call_t proxy = {&state, device_endpoint_configure_proxy_submit};
    return executor->invoke(executor->context, &proxy, timeout_ms);
  }
  if (endpoint->private_state.stepping || endpoint->private_state.closing ||
      endpoint->private_state.sync_waiting) {
    state.result.local_error = WL_ERR_REENTRANT;
    return state.result;
  }
  if (wl_endpoint_link(device_endpoint_handle(endpoint)) == NULL) {
    state.result.local_error = WL_ERR_NOT_INITIALIZED;
    return state.result;
  }
  platform = wl_endpoint_waiter(device_endpoint_handle(endpoint));
  if (platform == NULL) {
    state.result.local_error = WL_ERR_NOT_SUPPORTED;
    return state.result;
  }
  waiter = *platform;
  endpoint->private_state.sync_waiting = true;
  error = device_endpoint_configure_async(endpoint, request, timeout_ms,
      device_endpoint_configure_sync_done, &state, &call);
  if (error == WL_OK) {
    while (!state.done) {
      wl_poll_hint_t hint;
      error = device_endpoint_step(endpoint);
      if (state.done || error != WL_OK) break;
      error = wl_endpoint_get_hint(device_endpoint_handle(endpoint), &hint);
      if (error != WL_OK) break;
      if (hint.work_pending || hint.next_deadline_ms == 0U) continue;
      error = waiter.wait(waiter.user_data, hint.next_deadline_ms);
      if (error == WL_ERR_NO_DATA) error = WL_OK;
      if (error != WL_OK) break;
    }
    if (!state.done) {
      /* The accepted handle is still live. Shared notification machinery
       * detaches this stack context without closing unrelated calls. */
      endpoint->private_state.stepping = true;
      (void)wl_rpc_async_cancel_complete(&endpoint->private_state.async, &call);
      endpoint->private_state.stepping = false;
      state.result.status = error == WL_ERR_CANCELLED ? WL_RPC_CANCELLED : WL_RPC_FAILED;
      state.result.local_error = error;
    }
  } else {
    state.result.local_error = error;
  }
  endpoint->private_state.sync_waiting = false;
  return state.result;
}
/* Generator internals: all services reuse one bounded completion scratch. */
static inline void device_endpoint_get_info_prepare(void *context,
    const wl_rpc_client_result_t *client) {
  device_endpoint_t *endpoint = (device_endpoint_t *)context;
  wl_rpc_completion_t *result = &endpoint->private_state.completion;
  wl_rpc_async_completion(client, result);
  if (result->status == WL_RPC_SUCCESS) {
    if (client->response_length < 20U) {
      result->status = WL_RPC_FAILED;
      result->runtime_error = WL_RPC_ERR_MALFORMED_METADATA;
      return;
    }
    result->codec_error = info_response_value_decode(client->response_data + 20U,
        client->response_length - 20U, &endpoint->private_state.values.get_info.response);
    if (result->codec_error != WL_CODEC_OK) result->status = WL_RPC_FAILED;
  }
}
static inline void device_endpoint_get_info_notify(void *context,
    wl_rpc_callback_t callback, void *user_data) {
  device_endpoint_t *endpoint = (device_endpoint_t *)context;
  ((device_get_info_completion_fn)callback)(user_data, &endpoint->private_state.completion,
      endpoint->private_state.completion.status == WL_RPC_SUCCESS
          ? &endpoint->private_state.values.get_info.response : NULL);
}

/* Snapshot and accept a call; callback is delivered by step or orderly close.
 * out_call is optional cancellation authority. Successful completion owns its
 * fields; copy *response during the callback to save it. Never release a slot.
 * No callback on failed admission; BUSY means local bounded capacity is full. */
static inline wl_err_t device_endpoint_get_info_submit_at(device_endpoint_t *endpoint,
    const info_request_value_t *request, uint32_t timeout_ms, wl_time_ms_t now_ms,
    device_get_info_completion_fn callback, void *user_data, wl_rpc_call_t *out_call) {
  wl_rpc_async_observer_t observer;
  if (endpoint == NULL || request == NULL || callback == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_state.closing) return WL_ERR_NOT_INITIALIZED;
  observer.prepare = device_endpoint_get_info_prepare;
  observer.notify = device_endpoint_get_info_notify;
  observer.context = endpoint;
  observer.callback = (wl_rpc_callback_t)callback;
  observer.user_data = user_data;
  return wl_rpc_async_submit(&endpoint->private_state.async, 2U, 4U,
      WL_DELIVERY_RELIABLE, timeout_ms, now_ms, device_get_info_encode_submission, request,
      &observer, out_call);
}

static inline wl_err_t device_endpoint_get_info_async(device_endpoint_t *endpoint,
    const info_request_value_t *request, uint32_t timeout_ms,
    device_get_info_completion_fn callback, void *user_data, wl_rpc_call_t *out_call) {
  wl_time_ms_t now_ms;
  int error;
  if (endpoint == NULL || request == NULL || callback == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_state.closing) return WL_ERR_NOT_INITIALIZED;
  error = wl_endpoint_now(device_endpoint_handle(endpoint), &now_ms);
  if (error != WL_OK) return error;
  return device_endpoint_get_info_submit_at(endpoint, request, timeout_ms, now_ms,
      callback, user_data, out_call);
}

/* Internal stack completion; no large response temporary or borrowed fields. */
typedef struct {
  bool done;
  wl_rpc_completion_t result;
  info_response_value_t *response;
  device_endpoint_t *endpoint;
  const info_request_value_t *request;
  wl_rpc_sync_notify_fn notify;
  void *notify_context;
} device_get_info_sync_state_t;

static inline void device_endpoint_get_info_sync_done(void *context,
    const wl_rpc_completion_t *result, const info_response_value_t *response) {
  device_get_info_sync_state_t *state = (device_get_info_sync_state_t *)context;
  state->result = *result;
  if (response != NULL) *state->response = *response;
  state->done = true;
  if (state->notify != NULL) state->notify(state->notify_context, &state->result);
}

static inline wl_err_t device_endpoint_get_info_proxy_submit(void *context,
    wl_time_ms_t deadline, wl_rpc_sync_notify_fn notify, void *notify_context,
    wl_rpc_call_t *call) {
  device_get_info_sync_state_t *state = (device_get_info_sync_state_t *)context;
  wl_time_ms_t now_ms;
  int error = wl_endpoint_now(device_endpoint_handle(state->endpoint), &now_ms);
  if (error != WL_OK) return error;
  const uint32_t remaining = deadline - now_ms;
  if (remaining == 0U || remaining > INT32_MAX) return WL_ERR_TIMEOUT;
  state->notify = notify;
  state->notify_context = notify_context;
  return device_endpoint_get_info_submit_at(state->endpoint, state->request, remaining, now_ms,
      device_endpoint_get_info_sync_done, state, call);
}

/* Platform call: use the installed waiter on the owner thread, or the bound
 * executor's proxy from business threads. Reuses async deadlines/completion;
 * never call from the same owner's callbacks. Response changes only on SUCCESS. Admission
 * and platform failures use local_error, not business rejection/transport_error.
 * On return there is no remaining callback referring to this function's stack. */
static inline wl_rpc_completion_t device_endpoint_get_info_sync(device_endpoint_t *endpoint,
    const info_request_value_t *request, info_response_value_t *response, uint32_t timeout_ms) {
  device_get_info_sync_state_t state;
  wl_rpc_call_t call;
  wl_waiter_t waiter;
  const wl_waiter_t *platform;
  const wl_rpc_executor_t *executor;
  wl_err_t error;
  memset(&state, 0, sizeof(state));
  state.result.status = WL_RPC_FAILED;
  state.response = response;
  state.endpoint = endpoint;
  state.request = request;
  if (endpoint == NULL || request == NULL || response == NULL) {
    state.result.local_error = WL_ERR_INVALID_ARG;
    return state.result;
  }
  /* The binding is immutable while callers run. Do not inspect mutable owner
   * state on a proxy caller thread, including during executor shutdown. */
  executor = wl_endpoint_rpc_executor(device_endpoint_handle(endpoint));
  if (executor != NULL) {
    const wl_rpc_sync_call_t proxy = {&state, device_endpoint_get_info_proxy_submit};
    return executor->invoke(executor->context, &proxy, timeout_ms);
  }
  if (endpoint->private_state.stepping || endpoint->private_state.closing ||
      endpoint->private_state.sync_waiting) {
    state.result.local_error = WL_ERR_REENTRANT;
    return state.result;
  }
  if (wl_endpoint_link(device_endpoint_handle(endpoint)) == NULL) {
    state.result.local_error = WL_ERR_NOT_INITIALIZED;
    return state.result;
  }
  platform = wl_endpoint_waiter(device_endpoint_handle(endpoint));
  if (platform == NULL) {
    state.result.local_error = WL_ERR_NOT_SUPPORTED;
    return state.result;
  }
  waiter = *platform;
  endpoint->private_state.sync_waiting = true;
  error = device_endpoint_get_info_async(endpoint, request, timeout_ms,
      device_endpoint_get_info_sync_done, &state, &call);
  if (error == WL_OK) {
    while (!state.done) {
      wl_poll_hint_t hint;
      error = device_endpoint_step(endpoint);
      if (state.done || error != WL_OK) break;
      error = wl_endpoint_get_hint(device_endpoint_handle(endpoint), &hint);
      if (error != WL_OK) break;
      if (hint.work_pending || hint.next_deadline_ms == 0U) continue;
      error = waiter.wait(waiter.user_data, hint.next_deadline_ms);
      if (error == WL_ERR_NO_DATA) error = WL_OK;
      if (error != WL_OK) break;
    }
    if (!state.done) {
      /* The accepted handle is still live. Shared notification machinery
       * detaches this stack context without closing unrelated calls. */
      endpoint->private_state.stepping = true;
      (void)wl_rpc_async_cancel_complete(&endpoint->private_state.async, &call);
      endpoint->private_state.stepping = false;
      state.result.status = error == WL_ERR_CANCELLED ? WL_RPC_CANCELLED : WL_RPC_FAILED;
      state.result.local_error = error;
    }
  } else {
    state.result.local_error = error;
  }
  endpoint->private_state.sync_waiting = false;
  return state.result;
}
#ifdef __cplusplus
}
#endif

#endif
