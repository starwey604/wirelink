#include "device_runtime.h"

#include <string.h>

static device_runtime_result_t device_runtime_result(const wl_event_t *event) {
  device_runtime_result_t result = {0};
  result.domain = DEVICE_RUNTIME_INVALID_ARGUMENT;
  if (event != NULL) {
    result.message_id = event->message_id;
    result.event_type = event->type;
  }
  return result;
}

const char *device_runtime_result_str(const device_runtime_result_t *result) {
  if (result == NULL) return "null result";
  switch (result->domain) {
    case DEVICE_RUNTIME_OK: return "ok";
    case DEVICE_RUNTIME_NON_RX: return "non-rx event";
    case DEVICE_RUNTIME_UNKNOWN_MESSAGE: return "unknown message";
    case DEVICE_RUNTIME_MISSING_ROUTE: return "missing route";
    case DEVICE_RUNTIME_MISSING_SCRATCH: return "missing scratch";
    case DEVICE_RUNTIME_DELIVERY_MISMATCH: return "delivery mismatch";
    case DEVICE_RUNTIME_CODEC_ERROR: return "codec error";
    case DEVICE_RUNTIME_STORAGE_ERROR: return "storage error";
    case DEVICE_RUNTIME_RPC_ERROR: return "rpc error";
    case DEVICE_RUNTIME_CORE_ERROR: return "core error";
    case DEVICE_RUNTIME_APPLICATION_ERROR: return "application error";
    case DEVICE_RUNTIME_INVALID_ARGUMENT: return "invalid argument";
    default: return "unknown runtime result";
  }
}

/* Managed RPC v2: 00 02 kind 00, BE32 correlation, BE32 signed status,
 * BE64 originating client session, then business payload.
 * The zero discriminator is not a valid legacy codec field tag. */
static uint32_t device_rpc_read_u32(const uint8_t *data) {
  return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
         ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}
static void device_rpc_write_u32(uint8_t *data, uint32_t value) {
  data[0] = (uint8_t)(value >> 24U);
  data[1] = (uint8_t)(value >> 16U);
  data[2] = (uint8_t)(value >> 8U);
  data[3] = (uint8_t)value;
}
static wl_rpc_err_t device_rpc_header_read(const uint8_t *data, size_t length,
    uint8_t kind, uint32_t *operation_id, int32_t *status, uint64_t *session) {
  uint32_t bits;
  if (data == NULL || length < 20U || data[0] != 0U || data[1] != 2U ||
      data[2] != kind || data[3] != 0U) return WL_RPC_ERR_MALFORMED_METADATA;
  *operation_id = device_rpc_read_u32(data + 4U);
  bits = device_rpc_read_u32(data + 8U);
  *session = ((uint64_t)device_rpc_read_u32(data + 12U) << 32U) |
      device_rpc_read_u32(data + 16U);
  *status = bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
  if (*session == 0U || *operation_id == 0U || (kind == 1U && *status != 0))
    return WL_RPC_ERR_MALFORMED_METADATA;
  return WL_RPC_OK;
}
static void device_rpc_header_write(uint8_t *data, uint8_t kind,
    uint32_t operation_id, int32_t status, uint64_t session) {
  data[0] = 0U; data[1] = 2U; data[2] = kind; data[3] = 0U;
  device_rpc_write_u32(data + 4U, operation_id);
  device_rpc_write_u32(data + 8U, (uint32_t)status);
  device_rpc_write_u32(data + 12U, (uint32_t)(session >> 32U));
  device_rpc_write_u32(data + 16U, (uint32_t)session);
}
/* Shared managed-RPC control flow. Typed adapters keep codec calls type-safe;
 * no function-pointer casts, heap, or extra persistent scratch are needed. */
typedef wl_codec_status_t (*device_rpc_response_encoder_fn)(const void *,
    uint8_t *, size_t, size_t *, bool);

#if DEVICE_RUNTIME_HAS_RPC_SERVER
static bool device_rpc_request_prepare(const wl_event_t *event,
    device_runtime_t *runtime, wl_event_type_t expected,
    device_runtime_result_t *result, uint64_t *session) {
  int32_t status = 0;
  result->detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  if (event->type != expected) {
    result->domain = DEVICE_RUNTIME_DELIVERY_MISMATCH;
    return false;
  }
  if (runtime->rpc_server == NULL) {
    result->domain = DEVICE_RUNTIME_MISSING_ROUTE;
    return false;
  }
  result->detail.rpc.rpc_result = device_rpc_header_read(event->payload,
      event->payload_len, 1U, &result->detail.rpc.operation_id, &status, session);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return false;
  }
  if (event->type == WL_EVT_RELIABLE_RX && event->peer_session_id != *session) {
    result->detail.rpc.rpc_result = WL_RPC_ERR_SESSION_MISMATCH;
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return false;
  }
  return true;
}

/* Return true only for a new request. Peer observation must remain after
 * successful decoding/fingerprinting: malformed input cannot cancel a peer. */
static bool device_rpc_request_begin(wl_ctx_t *ctx, device_runtime_t *runtime,
    const wl_rpc_request_identity_t *identity, size_t canonical_length,
    wl_time_ms_t now_ms, device_runtime_result_t *result) {
  wl_rpc_server_request_t request = {0};
  wl_rpc_server_response_t replay = {0};
  if (runtime->rpc_peer.session_id != identity->peer_session_id) {
    wl_rpc_peer_observation_t observation = {0};
    result->detail.rpc.rpc_result = device_runtime_peer_observe(ctx, runtime,
        identity->peer_session_id, &observation);
    if (result->detail.rpc.rpc_result != WL_RPC_OK) {
      result->domain = DEVICE_RUNTIME_RPC_ERROR;
      return false;
    }
    result->detail.rpc.peer_changed = observation.changed;
  }
  result->detail.rpc.payload_length = canonical_length + 20U;
  result->detail.rpc.rpc_result = wl_rpc_server_begin(runtime->rpc_server,
      identity, now_ms, &result->detail.rpc.rpc_disposition, &request, &replay);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return false;
  }
  switch (result->detail.rpc.rpc_disposition) {
    case WL_RPC_SERVER_NEW:
      result->detail.rpc.server_request = request;
      return true;
    case WL_RPC_SERVER_PENDING_DUPLICATE:
      result->domain = DEVICE_RUNTIME_OK;
      break;
    case WL_RPC_SERVER_REPLAY:
      result->detail.rpc.server_response = replay;
      result->detail.rpc.application_result = replay.application_status;
      result->detail.rpc.payload_length = replay.response_length;
      result->domain = DEVICE_RUNTIME_OK;
      break;
    case WL_RPC_SERVER_CONFLICT:
      result->detail.rpc.rpc_result = WL_RPC_ERR_OPERATION_CONFLICT;
      result->domain = DEVICE_RUNTIME_RPC_ERROR;
      break;
    default:
      result->detail.rpc.rpc_result = WL_RPC_ERR_INVALID_STATE;
      result->domain = DEVICE_RUNTIME_RPC_ERROR;
      break;
  }
  return false;
}
#endif

static void device_rpc_finish_response(device_runtime_t *runtime,
    const wl_rpc_server_request_t *request, int32_t application_status,
    const void *response, wl_time_ms_t now_ms, bool owned,
    device_rpc_response_encoder_fn encode, device_runtime_result_t *result) {
  wl_rpc_server_response_buffer_t buffer = {0};
  wl_rpc_server_response_t cached = {0};
  size_t encoded_length = 0U;
  result->detail.rpc.operation_id = request->identity.operation_id;
  result->detail.rpc.rpc_result = wl_rpc_server_response_prepare(runtime->rpc_server, request, &buffer);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return;
  }
  if (buffer.capacity < 20U) {
    result->detail.rpc.rpc_result = WL_RPC_ERR_RESPONSE_TOO_LARGE;
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return;
  }
  device_rpc_header_write(buffer.data, 2U, request->identity.operation_id,
      application_status, request->identity.peer_session_id);
  if (application_status == 0) {
    result->detail.rpc.codec_status = encode(response, buffer.data + 20U,
        buffer.capacity - 20U, &encoded_length, owned);
    if (result->detail.rpc.codec_status != WL_CODEC_OK) {
      result->domain = DEVICE_RUNTIME_CODEC_ERROR;
      return;
    }
  }
  result->detail.rpc.rpc_result = wl_rpc_server_response_commit(runtime->rpc_server,
      &buffer, application_status, encoded_length + 20U, now_ms, &cached);
  if (result->detail.rpc.rpc_result != WL_RPC_OK) {
    result->domain = DEVICE_RUNTIME_RPC_ERROR;
    return;
  }
  result->detail.rpc.server_response = cached;
  result->detail.rpc.payload_length = cached.response_length;
  result->domain = DEVICE_RUNTIME_OK;
}
static const uint64_t device_rpc_fingerprint_seed = UINT64_C(0x24faaea3493c1c2e);
wl_codec_status_t configure_request_wlc_detail_fingerprint(const configure_request_t *, uint64_t *, size_t *);
#if CONFIGURE_REQUEST_HAS_VALUE
void configure_request_wlc_detail_value_copy(const configure_request_t *, configure_request_value_t *);
#endif

wl_codec_status_t info_request_wlc_detail_fingerprint(const info_request_t *, uint64_t *, size_t *);
#if INFO_REQUEST_HAS_VALUE
void info_request_wlc_detail_value_copy(const info_request_t *, info_request_value_t *);
#endif

typedef struct { wl_ctx_t *link; device_runtime_t *runtime; } device_peer_cancel_context_t;
static void device_runtime_cancel_peer_tx(void *context, wl_tx_handle_t handle) {
  device_peer_cancel_context_t *cancel = context;
  wl_tx_result_t ignored;
  if (cancel == NULL) return;
  (void)wl_tx_cancel(cancel->link, handle);
  /* A cancelled transaction need not emit a terminal event. Take it now, or
   * retain just its handle until the adapter releases physical TX storage. */
  if (wl_tx_take(cancel->link, handle, &ignored) == WL_ERR_INVALID_STATE)
    cancel->runtime->rpc_retiring_tx = handle;
}

wl_err_t device_runtime_config_defaults(device_runtime_config_t *config) {
  if (config == NULL) return WL_ERR_INVALID_ARG;
  memset(config, 0, sizeof(*config));
  config->rpc_client_slot_count = 1U;
  config->rpc_client_next_operation_id = 1U;
  config->rpc_server_pending_slot_count = 1U;
  config->rpc_server_cache_slot_count = 1U;
  config->rpc_server_cache_policy = WL_RPC_CACHE_REJECT_NEW;
  config->rpc_client_response_capacity = 236U;
  config->rpc_server_response_capacity = 236U;
  return WL_OK;
}

wl_err_t device_runtime_config_enable_client(device_runtime_config_t *config) {
  if (config == NULL) return WL_ERR_INVALID_ARG;
  if (!DEVICE_RUNTIME_HAS_RPC_CLIENT || config->rpc_client_slot_count == 0U || config->rpc_client_response_capacity == 0U) return WL_ERR_NOT_SUPPORTED;
  config->rpc_client_enabled = 1U;
  return WL_OK;
}

wl_err_t device_runtime_config_enable_server(device_runtime_config_t *config) {
  if (config == NULL) return WL_ERR_INVALID_ARG;
  if (!DEVICE_RUNTIME_HAS_RPC_SERVER || config->rpc_server_pending_slot_count == 0U || config->rpc_server_cache_slot_count == 0U || config->rpc_server_response_capacity == 0U) return WL_ERR_NOT_SUPPORTED;
  config->rpc_server_enabled = 1U;
  return WL_OK;
}

device_runtime_storage_t device_runtime_default_storage_descriptor(device_runtime_default_storage_t *storage) {
  device_runtime_storage_t descriptor = {0};
  if (storage != NULL) {
    descriptor.data = storage->bytes;
    descriptor.size = sizeof(storage->bytes);
  }
  return descriptor;
}

static int device_runtime_roles_valid(const device_runtime_config_t *config) {
  (void)config;
  return WL_OK;
}

const char *device_runtime_init_issue_str(device_runtime_init_issue_t issue) {
  switch (issue) {
    case DEVICE_RUNTIME_INIT_OK: return "ok";
    case DEVICE_RUNTIME_INIT_NULL_ARGUMENT: return "null argument";
    case DEVICE_RUNTIME_INIT_ROLE_ENABLE: return "role enable must be zero or one";
    case DEVICE_RUNTIME_INIT_RETAINED_CAPACITY: return "retained capacity is zero";
    case DEVICE_RUNTIME_INIT_RPC_CLIENT_CAPACITY: return "RPC client capacity is zero";
    case DEVICE_RUNTIME_INIT_RPC_SERVER_CAPACITY: return "RPC server capacity is zero";
    case DEVICE_RUNTIME_INIT_RPC_TIMEOUT: return "RPC timeout exceeds wrap-safe range";
    case DEVICE_RUNTIME_INIT_RPC_CACHE_POLICY: return "unknown RPC cache policy";
    case DEVICE_RUNTIME_INIT_LAYOUT_OVERFLOW: return "runtime layout size overflow";
    case DEVICE_RUNTIME_INIT_STORAGE_TOO_SMALL: return "runtime storage is too small";
    case DEVICE_RUNTIME_INIT_STORAGE_NULL: return "runtime storage data is null";
    case DEVICE_RUNTIME_INIT_STORAGE_ALIGNMENT: return "runtime storage is misaligned";
    case DEVICE_RUNTIME_INIT_STORAGE_OVERLAP: return "runtime storage overlaps the instance";
    case DEVICE_RUNTIME_INIT_COMPONENT: return "runtime component initialization failed";
    default: return "unknown runtime initialization issue";
  }
}

static int device_runtime_init_failure(device_runtime_init_diagnostic_t *diagnostic, device_runtime_init_issue_t issue, const char *field, size_t required, size_t provided, int result) {
  if (diagnostic != NULL) {
    diagnostic->issue = issue;
    diagnostic->field = field;
    diagnostic->required = required;
    diagnostic->provided = provided;
  }
  return result;
}

typedef struct {
  uint8_t *base;
  size_t size;
  size_t offset;
} device_runtime_storage_cursor_t;

typedef struct {
  void *rpc_client_slots;
  void *rpc_client_responses;
  size_t rpc_client_responses_size;
  void *rpc_server_pending_slots;
  void *rpc_server_cache_slots;
  void *rpc_server_responses;
  size_t rpc_server_responses_size;
} device_runtime_layout_t;

static inline int device_runtime_storage_region(device_runtime_storage_cursor_t *cursor, size_t alignment, size_t count, size_t element_size, void **out_data, size_t *out_size) {
  size_t aligned;
  size_t region_size;
  if (cursor == NULL || alignment == 0U || (alignment & (alignment - 1U)) != 0U) return WL_ERR_INVALID_ARG;
  if (out_data != NULL) *out_data = NULL;
  if (out_size != NULL) *out_size = 0U;
  if (count != 0U && element_size > SIZE_MAX / count) return WL_ERR_INVALID_ARG;
  region_size = count * element_size;
  if (cursor->offset > SIZE_MAX - (alignment - 1U)) return WL_ERR_INVALID_ARG;
  aligned = (cursor->offset + (alignment - 1U)) & ~(alignment - 1U);
  if (region_size > SIZE_MAX - aligned) return WL_ERR_INVALID_ARG;
  if (aligned + region_size > cursor->size) return WL_ERR_BUF_TOO_SMALL;
  if (out_data != NULL && cursor->base != NULL) *out_data = cursor->base + aligned;
  if (out_size != NULL) *out_size = region_size;
  cursor->offset = aligned + region_size;
  return WL_OK;
}

static int device_runtime_layout(const device_runtime_config_t *config, uint8_t *base, size_t size, device_runtime_layout_t *out_layout, device_runtime_requirements_t *out_requirements) {
  device_runtime_storage_cursor_t cursor = {base, size, 0U};
  size_t alignment = 1U;
  int result;
  if (out_layout != NULL) memset(out_layout, 0, sizeof(*out_layout));
  if (out_requirements != NULL) memset(out_requirements, 0, sizeof(*out_requirements));
  if (config == NULL) return WL_ERR_INVALID_ARG;
  if (device_runtime_roles_valid(config) != WL_OK) return WL_ERR_NOT_SUPPORTED;
  if (config->rpc_client_enabled > 1U || config->rpc_server_enabled > 1U) return WL_ERR_INVALID_ARG;
  if (config->rpc_client_enabled != 0U) {
    if (config->rpc_client_slot_count == 0U || config->rpc_client_response_capacity == 0U) return WL_ERR_INVALID_ARG;
    if (alignment < _Alignof(wl_rpc_client_slot_t)) alignment = _Alignof(wl_rpc_client_slot_t);
    result = device_runtime_storage_region(&cursor, _Alignof(wl_rpc_client_slot_t), config->rpc_client_slot_count, sizeof(wl_rpc_client_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_client_slots, NULL);
    if (result != WL_OK) return result;
    result = device_runtime_storage_region(&cursor, 1U, config->rpc_client_slot_count, config->rpc_client_response_capacity, out_layout == NULL ? NULL : &out_layout->rpc_client_responses, out_layout == NULL ? NULL : &out_layout->rpc_client_responses_size);
    if (result != WL_OK) return result;
  }
  if (config->rpc_server_enabled != 0U) {
    if (config->rpc_server_pending_slot_count == 0U || config->rpc_server_cache_slot_count == 0U || config->rpc_server_response_capacity == 0U) return WL_ERR_INVALID_ARG;
    if ((config->rpc_server_pending_timeout_ms != 0U && config->rpc_server_pending_timeout_ms >= UINT32_C(0x80000000)) || (config->rpc_server_cache_ttl_ms != 0U && config->rpc_server_cache_ttl_ms >= UINT32_C(0x80000000))) return WL_ERR_INVALID_ARG;
    if (config->rpc_server_cache_policy != WL_RPC_CACHE_REJECT_NEW && config->rpc_server_cache_policy != WL_RPC_CACHE_EVICT_OLDEST) return WL_ERR_INVALID_ARG;
    if (alignment < _Alignof(wl_rpc_server_pending_slot_t)) alignment = _Alignof(wl_rpc_server_pending_slot_t);
    if (alignment < _Alignof(wl_rpc_server_cache_slot_t)) alignment = _Alignof(wl_rpc_server_cache_slot_t);
    result = device_runtime_storage_region(&cursor, _Alignof(wl_rpc_server_pending_slot_t), config->rpc_server_pending_slot_count, sizeof(wl_rpc_server_pending_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_server_pending_slots, NULL);
    if (result != WL_OK) return result;
    result = device_runtime_storage_region(&cursor, _Alignof(wl_rpc_server_cache_slot_t), config->rpc_server_cache_slot_count, sizeof(wl_rpc_server_cache_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_server_cache_slots, NULL);
    if (result != WL_OK) return result;
    result = device_runtime_storage_region(&cursor, 1U, config->rpc_server_cache_slot_count, config->rpc_server_response_capacity, out_layout == NULL ? NULL : &out_layout->rpc_server_responses, out_layout == NULL ? NULL : &out_layout->rpc_server_responses_size);
    if (result != WL_OK) return result;
  }
  if (out_requirements != NULL) {
    out_requirements->storage_size = cursor.offset;
    out_requirements->storage_alignment = alignment;
  }
  return WL_OK;
}

int device_runtime_requirements(const device_runtime_config_t *config, device_runtime_requirements_t *out_requirements) {
  device_runtime_config_t config_copy;
  if (config == NULL || out_requirements == NULL) return WL_ERR_INVALID_ARG;
  config_copy = *config;
  *out_requirements = (device_runtime_requirements_t){0};
  return device_runtime_layout(&config_copy, NULL, SIZE_MAX, NULL, out_requirements);
}

static int device_runtime_init_validate(const device_runtime_instance_t *instance, const device_runtime_config_t *config, const device_runtime_storage_t *storage, device_runtime_requirements_t *requirements, device_runtime_init_diagnostic_t *diagnostic) {
  uintptr_t instance_address;
  uintptr_t storage_address;
  int result;
  if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
  if (instance == NULL || config == NULL || storage == NULL || requirements == NULL || diagnostic == NULL)
    return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_NULL_ARGUMENT, instance == NULL ? "instance" : config == NULL ? "config" : storage == NULL ? "storage" : requirements == NULL ? "requirements" : "diagnostic", 1U, 0U, WL_ERR_INVALID_ARG);
  if (device_runtime_roles_valid(config) != WL_OK) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_ROLE_ENABLE, "endpoint.rpc_role", 0U, 1U, WL_ERR_NOT_SUPPORTED);
  if (config->rpc_client_enabled > 1U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_ROLE_ENABLE, "rpc_client_enabled", 1U, config->rpc_client_enabled, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled > 1U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_ROLE_ENABLE, "rpc_server_enabled", 1U, config->rpc_server_enabled, WL_ERR_INVALID_ARG);
  if (config->rpc_client_enabled != 0U && config->rpc_client_slot_count == 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_CLIENT_CAPACITY, "rpc_client_slot_count", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_client_enabled != 0U && config->rpc_client_response_capacity == 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_CLIENT_CAPACITY, "rpc_client_response_capacity", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled != 0U && config->rpc_server_pending_slot_count == 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_SERVER_CAPACITY, "rpc_server_pending_slot_count", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled != 0U && config->rpc_server_cache_slot_count == 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_SERVER_CAPACITY, "rpc_server_cache_slot_count", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled != 0U && config->rpc_server_response_capacity == 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_SERVER_CAPACITY, "rpc_server_response_capacity", 1U, 0U, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled != 0U && config->rpc_server_pending_timeout_ms >= UINT32_C(0x80000000)) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_TIMEOUT, "rpc_server_pending_timeout_ms", UINT32_C(0x7fffffff), config->rpc_server_pending_timeout_ms, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled != 0U && config->rpc_server_cache_ttl_ms >= UINT32_C(0x80000000)) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_TIMEOUT, "rpc_server_cache_ttl_ms", UINT32_C(0x7fffffff), config->rpc_server_cache_ttl_ms, WL_ERR_INVALID_ARG);
  if (config->rpc_server_enabled != 0U && config->rpc_server_cache_policy != WL_RPC_CACHE_REJECT_NEW && config->rpc_server_cache_policy != WL_RPC_CACHE_EVICT_OLDEST) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_RPC_CACHE_POLICY, "rpc_server_cache_policy", 0U, (size_t)config->rpc_server_cache_policy, WL_ERR_INVALID_ARG);
  result = device_runtime_requirements(config, requirements);
  if (result != WL_OK) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_LAYOUT_OVERFLOW, "config", 0U, 0U, result);
  if (storage->size < requirements->storage_size) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_TOO_SMALL, "storage.size", requirements->storage_size, storage->size, WL_ERR_BUF_TOO_SMALL);
  if (requirements->storage_size != 0U) {
    if (storage->data == NULL) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_NULL, "storage.data", requirements->storage_size, 0U, WL_ERR_INVALID_ARG);
    if (((uintptr_t)storage->data & (requirements->storage_alignment - 1U)) != 0U) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_ALIGNMENT, "storage.data", requirements->storage_alignment, (size_t)((uintptr_t)storage->data & (requirements->storage_alignment - 1U)), WL_ERR_INVALID_ARG);
    instance_address = (uintptr_t)(const void *)instance;
    storage_address = (uintptr_t)storage->data;
    if ((storage_address <= instance_address && instance_address - storage_address < requirements->storage_size) || (instance_address < storage_address && storage_address - instance_address < sizeof(*instance))) return device_runtime_init_failure(diagnostic, DEVICE_RUNTIME_INIT_STORAGE_OVERLAP, "storage.data", requirements->storage_size, storage->size, WL_ERR_INVALID_ARG);
  }
  return WL_OK;
}

int device_runtime_init(device_runtime_instance_t *instance, const device_runtime_config_t *config, const device_runtime_storage_t *storage) {
  device_runtime_config_t config_copy;
  device_runtime_storage_t storage_copy;
  device_runtime_requirements_t requirements;
  device_runtime_layout_t layout;
  uintptr_t instance_address;
  uintptr_t storage_address;
  int result;
  if (instance == NULL || config == NULL || storage == NULL) return WL_ERR_INVALID_ARG;
  config_copy = *config;
  storage_copy = *storage;
  config = &config_copy;
  storage = &storage_copy;
  result = device_runtime_requirements(config, &requirements);
  if (result != WL_OK) return result;
  if (storage->size < requirements.storage_size) return WL_ERR_BUF_TOO_SMALL;
  if (requirements.storage_size != 0U) {
    if (storage->data == NULL || ((uintptr_t)storage->data & (requirements.storage_alignment - 1U)) != 0U) return WL_ERR_INVALID_ARG;
    instance_address = (uintptr_t)(void *)instance;
    storage_address = (uintptr_t)storage->data;
    if ((storage_address <= instance_address && instance_address - storage_address < requirements.storage_size) || (instance_address < storage_address && storage_address - instance_address < sizeof(*instance))) return WL_ERR_INVALID_ARG;
  }
  result = device_runtime_layout(config, (uint8_t *)storage->data, storage->size, &layout, NULL);
  if (result != WL_OK) return result;
  memset(instance, 0, sizeof(*instance));
#if DEVICE_RUNTIME_HAS_RPC_CLIENT
  if (config->rpc_client_enabled != 0U) {
    const wl_rpc_client_config_t client_config = {
      (wl_rpc_client_slot_t *)layout.rpc_client_slots,
      config->rpc_client_slot_count,
      (uint8_t *)layout.rpc_client_responses,
      layout.rpc_client_responses_size,
      config->rpc_client_response_capacity,
      config->rpc_client_next_operation_id
    };
    if (wl_rpc_client_init(&instance->rpc_client, &client_config) != WL_RPC_OK) {
      result = WL_ERR_INVALID_ARG;
      goto init_failed;
    }
    instance->runtime.rpc_client = &instance->rpc_client;
  }
#endif
#if DEVICE_RUNTIME_HAS_RPC_SERVER
  if (config->rpc_server_enabled != 0U) {
    const wl_rpc_server_config_t server_config = {
      (wl_rpc_server_pending_slot_t *)layout.rpc_server_pending_slots,
      config->rpc_server_pending_slot_count,
      (wl_rpc_server_cache_slot_t *)layout.rpc_server_cache_slots,
      config->rpc_server_cache_slot_count,
      (uint8_t *)layout.rpc_server_responses,
      layout.rpc_server_responses_size,
      config->rpc_server_response_capacity,
      config->rpc_server_pending_timeout_ms,
      config->rpc_server_cache_ttl_ms,
      config->rpc_server_cache_policy
    };
    if (wl_rpc_server_init(&instance->rpc_server, &server_config) != WL_RPC_OK) {
      result = WL_ERR_INVALID_ARG;
      goto init_failed;
    }
    instance->runtime.rpc_server = &instance->rpc_server;
  }
#endif
#if DEVICE_RUNTIME_HAS_RPC_SERVER
  if (config->rpc_server_enabled != 0U) {
    instance->runtime.configure.request_scratch = &instance->configure_scratch.request;
    instance->runtime.configure.request_handler = config->configure_request_handler;
    instance->runtime.configure.user_data = config->configure_user_data;
  }
#endif
#if DEVICE_RUNTIME_HAS_RPC_CLIENT
  if (config->rpc_client_enabled != 0U) instance->runtime.configure.response_scratch = &instance->configure_scratch.response;
#endif
#if DEVICE_RUNTIME_HAS_RPC_SERVER
  if (config->rpc_server_enabled != 0U) {
    instance->runtime.get_info.request_scratch = &instance->get_info_scratch.request;
    instance->runtime.get_info.request_handler = config->get_info_request_handler;
    instance->runtime.get_info.user_data = config->get_info_user_data;
  }
#endif
#if DEVICE_RUNTIME_HAS_RPC_CLIENT
  if (config->rpc_client_enabled != 0U) instance->runtime.get_info.response_scratch = &instance->get_info_scratch.response;
#endif
  return WL_OK;

init_failed:
  memset(instance, 0, sizeof(*instance));
  return result;
}

int device_runtime_init_checked(device_runtime_instance_t *instance, const device_runtime_config_t *config, const device_runtime_storage_t *storage, device_runtime_init_diagnostic_t *out_diagnostic) {
  device_runtime_requirements_t requirements;
  int result = device_runtime_init_validate(instance, config, storage, &requirements, out_diagnostic);
  if (result != WL_OK) return result;
  result = device_runtime_init(instance, config, storage);
  if (result != WL_OK) return device_runtime_init_failure(out_diagnostic, DEVICE_RUNTIME_INIT_COMPONENT, "component", 0U, 0U, result);
  return WL_OK;
}

wl_rpc_err_t device_runtime_peer_observe(wl_ctx_t *ctx, device_runtime_t *runtime, uint64_t peer_session_id, wl_rpc_peer_observation_t *out_observation) {
  device_peer_cancel_context_t cancel = {ctx, runtime};
  wl_rpc_err_t result;
  if (out_observation != NULL) memset(out_observation, 0, sizeof(*out_observation));
  if (ctx == NULL || runtime == NULL || runtime->rpc_server == NULL || peer_session_id == 0U || out_observation == NULL) return WL_RPC_ERR_INVALID_ARG;
  result = wl_rpc_peer_observe(runtime->rpc_server, &runtime->rpc_peer, peer_session_id, device_runtime_cancel_peer_tx, &cancel, out_observation);
  if (result == WL_RPC_OK && out_observation->changed != 0U) runtime->rpc_peer_observation = *out_observation;
  return result;
}

wl_rpc_err_t device_runtime_peer_observation_take(device_runtime_t *runtime, wl_rpc_peer_observation_t *out_observation) {
  if (out_observation != NULL) memset(out_observation, 0, sizeof(*out_observation));
  if (runtime == NULL || out_observation == NULL) return WL_RPC_ERR_INVALID_ARG;
  if (runtime->rpc_peer_observation.changed == 0U) return WL_RPC_ERR_NOT_FOUND;
  *out_observation = runtime->rpc_peer_observation;
  memset(&runtime->rpc_peer_observation, 0, sizeof(runtime->rpc_peer_observation));
  return WL_RPC_OK;
}

wl_rpc_err_t device_runtime_poll(device_runtime_t *runtime, wl_time_ms_t now_ms, device_runtime_poll_result_t *out_result) {
  wl_rpc_err_t result;
  wl_rpc_server_expiry_t server_expiry = {0};
  if (out_result != NULL) memset(out_result, 0, sizeof(*out_result));
  if (runtime == NULL || out_result == NULL) return WL_RPC_ERR_INVALID_ARG;
  if (runtime->rpc_client != NULL) {
    result = wl_rpc_client_poll(runtime->rpc_client, now_ms, &out_result->client_timed_out);
    if (result != WL_RPC_OK) return result;
  }
  if (runtime->rpc_server != NULL) {
    result = wl_rpc_server_expired_acquire(runtime->rpc_server, now_ms, &out_result->server_expired_request);
    if (result == WL_RPC_OK) out_result->server_pending_expired = 1U;
    else if (result != WL_RPC_ERR_NOT_FOUND) return result;
    result = wl_rpc_server_poll(runtime->rpc_server, now_ms, &server_expiry);
    if (result != WL_RPC_OK) return result;
    out_result->server_cache_expired = server_expiry.cache_expired;
  }
  return WL_RPC_OK;
}

wl_rpc_err_t device_runtime_service(wl_ctx_t *ctx, device_runtime_t *runtime, wl_time_ms_t now_ms, device_runtime_service_result_t *out_result) {
  wl_rpc_server_response_t response = {0};
  wl_rpc_err_t result;
  uint8_t reliable_response = 0U;
  if (out_result != NULL) memset(out_result, 0, sizeof(*out_result));
  if (ctx == NULL || runtime == NULL || out_result == NULL) return WL_RPC_ERR_INVALID_ARG;
  out_result->response = device_runtime_result(NULL);
  if (runtime->rpc_retiring_tx != 0U) {
    wl_tx_result_t ignored;
    int retired = wl_tx_take(ctx, runtime->rpc_retiring_tx, &ignored);
    if (retired == WL_OK || retired == WL_ERR_NOT_FOUND) runtime->rpc_retiring_tx = 0U;
  }
  result = device_runtime_poll(runtime, now_ms, &out_result->deadlines);
  if (result != WL_RPC_OK) return result;
  if (runtime->rpc_server == NULL) return WL_RPC_OK;
  result = wl_rpc_server_response_acquire(runtime->rpc_server, &response);
  if (result == WL_RPC_ERR_NOT_FOUND) return WL_RPC_OK;
  if (result != WL_RPC_OK) return result;
  out_result->response.message_id = response.identity.response_message_id;
  out_result->response.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  out_result->response.detail.rpc.operation_id = response.identity.operation_id;
  out_result->response.detail.rpc.application_result = response.application_status;
  out_result->response.detail.rpc.payload_length = response.response_length;
  out_result->response.detail.rpc.server_response = response;
  switch (response.identity.response_message_id) {
    case CONFIGURE_RESPONSE_MESSAGE_ID:
      if (response.identity.request_message_id != CONFIGURE_REQUEST_MESSAGE_ID) {
        result = WL_RPC_ERR_RESPONSE_MISMATCH;
        break;
      }
      reliable_response = 1U;
      break;
    case INFO_RESPONSE_MESSAGE_ID:
      if (response.identity.request_message_id != INFO_REQUEST_MESSAGE_ID) {
        result = WL_RPC_ERR_RESPONSE_MISMATCH;
        break;
      }
      reliable_response = 1U;
      break;
    default:
      result = WL_RPC_ERR_RESPONSE_MISMATCH;
      break;
  }
  if (result != WL_RPC_OK) {
    (void)wl_rpc_server_response_defer(runtime->rpc_server, &response);
    return result;
  }
  if (reliable_response != 0U) {
    out_result->response.detail.rpc.core_result = wl_send_reliable(ctx, response.identity.response_message_id, response.response_data, response.response_length, now_ms, &out_result->response.detail.rpc.handle);
  } else {
    out_result->response.detail.rpc.core_result = wl_send_unreliable(ctx, response.identity.response_message_id, response.response_data, response.response_length);
  }
  if (out_result->response.detail.rpc.core_result != WL_OK) {
    result = wl_rpc_server_response_defer(runtime->rpc_server, &response);
    if (result != WL_RPC_OK) return result;
    out_result->response.domain = DEVICE_RUNTIME_CORE_ERROR;
    out_result->responses_deferred = 1U;
    return WL_RPC_OK;
  }
  if (reliable_response != 0U) {
    result = wl_rpc_server_response_submitted(runtime->rpc_server, &response, out_result->response.detail.rpc.handle);
  } else {
    result = wl_rpc_server_response_sent(runtime->rpc_server, &response);
  }
  if (result != WL_RPC_OK) {
    (void)wl_rpc_server_response_defer(runtime->rpc_server, &response);
    return result;
  }
  out_result->response.domain = DEVICE_RUNTIME_OK;
  out_result->responses_submitted = 1U;
  return WL_RPC_OK;
}

wl_rpc_err_t device_runtime_get_deadline_hint(const device_runtime_t *runtime, wl_time_ms_t now_ms, wl_rpc_deadline_hint_t *out_hint) {
  wl_rpc_deadline_hint_t component = {WL_RPC_NO_DEADLINE_MS};
  wl_rpc_err_t result;
  uint32_t nearest = WL_RPC_NO_DEADLINE_MS;
  if (out_hint != NULL) out_hint->next_deadline_ms = WL_RPC_NO_DEADLINE_MS;
  if (runtime == NULL || out_hint == NULL) return WL_RPC_ERR_INVALID_ARG;
  if (runtime->rpc_client != NULL) {
    result = wl_rpc_client_get_deadline_hint(runtime->rpc_client, now_ms, &component);
    if (result != WL_RPC_OK) return result;
    if (component.next_deadline_ms < nearest) nearest = component.next_deadline_ms;
  }
  if (runtime->rpc_server != NULL) {
    result = wl_rpc_server_get_deadline_hint(runtime->rpc_server, now_ms, &component);
    if (result != WL_RPC_OK) return result;
    if (component.next_deadline_ms < nearest) nearest = component.next_deadline_ms;
  }
  out_hint->next_deadline_ms = nearest;
  return WL_RPC_OK;
}

device_runtime_result_t device_runtime_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, device_runtime_t *runtime, wl_time_ms_t now_ms) {
  device_runtime_result_t result = device_runtime_result(event);
  if (event == NULL) return result;
  (void)now_ms;
  if (event->type == WL_EVT_TX_SUCCESS || event->type == WL_EVT_TX_TIMEOUT || event->type == WL_EVT_TX_FAILED) {
    wl_tx_result_t tx_result = {0};
    if (runtime == NULL || ctx == NULL) {
      result.domain = DEVICE_RUNTIME_NON_RX;
      return result;
    }
    result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
    result.detail.rpc.handle = event->handle;
#if DEVICE_RUNTIME_HAS_MANAGED_RPC
    if (runtime->rpc_async != NULL && wl_rpc_async_retire_tx(runtime->rpc_async, event->handle)) {
      result.detail.rpc.core_result = wl_tx_take(ctx, event->handle, &tx_result);
      result.event_consumed = result.detail.rpc.core_result == WL_OK ? 1U : 0U;
      result.domain = result.detail.rpc.core_result == WL_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_CORE_ERROR;
      return result;
    }
#endif
    if (runtime->rpc_server != NULL) {
      result.detail.rpc.rpc_result = wl_rpc_server_on_tx_event(runtime->rpc_server, event);
      if (result.detail.rpc.rpc_result == WL_RPC_OK) {
        result.detail.rpc.core_result = wl_tx_take(ctx, event->handle, &tx_result);
        result.event_consumed = result.detail.rpc.core_result == WL_OK ? 1U : 0U;
        result.domain = result.detail.rpc.core_result == WL_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_CORE_ERROR;
        return result;
      }
      if (result.detail.rpc.rpc_result != WL_RPC_ERR_NOT_FOUND) {
        result.domain = DEVICE_RUNTIME_RPC_ERROR;
        return result;
      }
    }
    if (runtime->rpc_client != NULL) {
      result.detail.rpc.rpc_result = wl_rpc_client_on_tx_event(runtime->rpc_client, event);
      if (result.detail.rpc.rpc_result == WL_RPC_OK) {
        result.detail.rpc.core_result = wl_tx_take(ctx, event->handle, &tx_result);
        result.event_consumed = result.detail.rpc.core_result == WL_OK ? 1U : 0U;
        result.domain = result.detail.rpc.core_result == WL_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_CORE_ERROR;
      } else if (result.detail.rpc.rpc_result == WL_RPC_ERR_NOT_FOUND) result.domain = DEVICE_RUNTIME_NON_RX;
      else result.domain = DEVICE_RUNTIME_RPC_ERROR;
    } else {
      result.domain = DEVICE_RUNTIME_NON_RX;
    }
    return result;
  }
  if (event->type != WL_EVT_UNRELIABLE_RX && event->type != WL_EVT_RELIABLE_RX) {
    result.domain = DEVICE_RUNTIME_NON_RX;
    return result;
  }
  if (ctx == NULL) return result;
  if (runtime == NULL) goto release_event;

  switch (event->message_id) {
    case 5U: {
      wl_rpc_request_identity_t identity = {.request_fingerprint = device_rpc_fingerprint_seed};
      wl_rpc_server_request_t server_request;
      device_configure_request_token_t token;
      size_t canonical_length = 0U;
      uint64_t session = 0U;
      if (!device_rpc_request_prepare(event, runtime, WL_EVT_RELIABLE_RX, &result, &session)) break;
      if (runtime->configure.request_scratch == NULL) {
        result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
        break;
      }
      result.detail.rpc.codec_status = configure_request_decode(event->payload + 20U,
          event->payload_len - 20U, runtime->configure.request_scratch);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_RUNTIME_CODEC_ERROR;
        break;
      }
      result.detail.rpc.codec_status = configure_request_wlc_detail_fingerprint(runtime->configure.request_scratch,
          &identity.request_fingerprint, &canonical_length);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_RUNTIME_CODEC_ERROR;
        break;
      }
      identity.operation_id = result.detail.rpc.operation_id;
      identity.request_message_id = 5U;
      identity.response_message_id = 6U;
      identity.peer_session_id = session;
      if (!device_rpc_request_begin(ctx, runtime, &identity, canonical_length, now_ms, &result)) break;
      server_request = result.detail.rpc.server_request;
      memset(&token, 0, sizeof(token));
      token.private_state.owner = runtime;
      token.private_state.incarnation = runtime->rpc_incarnation;
      token.private_state.request = server_request;
#if CONFIGURE_REQUEST_HAS_VALUE && CONFIGURE_RESPONSE_HAS_VALUE
      if (runtime->configure.value_handler != NULL) {
        int32_t status;
        device_runtime_result_t completed;
        if (runtime->configure.request_value == NULL || runtime->configure.response_value == NULL) {
          (void)wl_rpc_server_abandon(runtime->rpc_server, &server_request);
          result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
          break;
        }
        /* The view was decoded and has not crossed an application callback. */
        configure_request_wlc_detail_value_copy(
            runtime->configure.request_scratch, runtime->configure.request_value);
        configure_response_value_clear(runtime->configure.response_value);
        status = runtime->configure.value_handler(runtime->configure.value_user_data,
            runtime->configure.request_value, runtime->configure.response_value);
        completed = status == 0
            ? device_configure_server_complete_value(runtime, &token, runtime->configure.response_value, now_ms)
            : device_configure_server_reject(runtime, &token, status, now_ms);
        result.domain = completed.domain;
        result.detail.rpc = completed.detail.rpc;
        if (!device_runtime_result_ok(&completed))
          (void)wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        break;
      }
#endif
      if (runtime->configure.request_handler == NULL) {
        result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        result.domain = DEVICE_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.rpc.application_result = runtime->configure.request_handler(
          runtime->configure.user_data, runtime->configure.request_scratch, &token, WL_DELIVERY_RELIABLE);
      if (result.detail.rpc.application_result != 0) {
        result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        result.domain = DEVICE_RUNTIME_APPLICATION_ERROR;
      } else result.domain = DEVICE_RUNTIME_OK;
      break;
    }
    case 6U: {
      wl_rpc_client_result_t client;
      uint64_t session = 0U;
      result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
      if (event->type != WL_EVT_RELIABLE_RX) {
        result.domain = DEVICE_RUNTIME_DELIVERY_MISMATCH;
        break;
      }
      if (runtime->rpc_client == NULL) {
        result.domain = DEVICE_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.rpc.rpc_result = device_rpc_header_read(event->payload,
          event->payload_len, 2U, &result.detail.rpc.operation_id,
          &result.detail.rpc.application_result, &session);
      if (result.detail.rpc.rpc_result != WL_RPC_OK ||
          (result.detail.rpc.application_result != 0 && event->payload_len != 20U)) {
        result.detail.rpc.rpc_result = WL_RPC_ERR_MALFORMED_METADATA;
        result.domain = DEVICE_RUNTIME_RPC_ERROR;
        break;
      }
      if (session != wl_link_session_id(ctx)) {
        /* Observable stale/misdirected reply; do not touch another call. */
        result.detail.rpc.rpc_result = WL_RPC_ERR_SESSION_MISMATCH;
        result.domain = DEVICE_RUNTIME_OK;
        break;
      }
      if (runtime->rpc_async != NULL) {
        uint16_t expired;
        /* A reply received at/after the acceptance deadline cannot resurrect
         * that call, even though event dispatch precedes owner progress. */
        (void)wl_rpc_client_poll(runtime->rpc_client, now_ms, &expired);
      }
      result.detail.rpc.rpc_result = wl_rpc_client_get(runtime->rpc_client,
          result.detail.rpc.operation_id, &client);
      if (result.detail.rpc.rpc_result == WL_RPC_ERR_NOT_FOUND) {
        /* Unknown/late responses are observable, not failures of another call. */
        result.domain = DEVICE_RUNTIME_OK;
        break;
      }
      if (result.detail.rpc.rpc_result != WL_RPC_OK ||
          client.request_message_id != 5U || client.response_message_id != 6U) {
        result.detail.rpc.rpc_result = WL_RPC_ERR_RESPONSE_MISMATCH;
        result.domain = DEVICE_RUNTIME_RPC_ERROR;
        break;
      }
      if (client.state != WL_RPC_CLIENT_LINK_PENDING && client.state != WL_RPC_CLIENT_WAIT_RESPONSE) {
        result.detail.rpc.rpc_result = WL_RPC_ERR_INVALID_STATE;
        result.domain = DEVICE_RUNTIME_OK;
        break;
      }
      if (result.detail.rpc.application_result == 0) {
        if (runtime->configure.response_scratch == NULL) {
          result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
          break;
        }
        result.detail.rpc.codec_status = configure_response_decode(event->payload + 20U,
            event->payload_len - 20U, runtime->configure.response_scratch);
        if (result.detail.rpc.codec_status != WL_CODEC_OK) {
          result.domain = DEVICE_RUNTIME_CODEC_ERROR;
          break;
        }
      }
      result.detail.rpc.payload_length = event->payload_len;
      result.detail.rpc.rpc_result = wl_rpc_client_on_response(runtime->rpc_client,
          6U, result.detail.rpc.operation_id, result.detail.rpc.application_result,
          event->payload, event->payload_len);
      result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ?
          DEVICE_RUNTIME_OK : DEVICE_RUNTIME_RPC_ERROR;
      break;
    }
    case 2U: {
      wl_rpc_request_identity_t identity = {.request_fingerprint = device_rpc_fingerprint_seed};
      wl_rpc_server_request_t server_request;
      device_get_info_request_token_t token;
      size_t canonical_length = 0U;
      uint64_t session = 0U;
      if (!device_rpc_request_prepare(event, runtime, WL_EVT_RELIABLE_RX, &result, &session)) break;
      if (runtime->get_info.request_scratch == NULL) {
        result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
        break;
      }
      result.detail.rpc.codec_status = info_request_decode(event->payload + 20U,
          event->payload_len - 20U, runtime->get_info.request_scratch);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_RUNTIME_CODEC_ERROR;
        break;
      }
      result.detail.rpc.codec_status = info_request_wlc_detail_fingerprint(runtime->get_info.request_scratch,
          &identity.request_fingerprint, &canonical_length);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_RUNTIME_CODEC_ERROR;
        break;
      }
      identity.operation_id = result.detail.rpc.operation_id;
      identity.request_message_id = 2U;
      identity.response_message_id = 4U;
      identity.peer_session_id = session;
      if (!device_rpc_request_begin(ctx, runtime, &identity, canonical_length, now_ms, &result)) break;
      server_request = result.detail.rpc.server_request;
      memset(&token, 0, sizeof(token));
      token.private_state.owner = runtime;
      token.private_state.incarnation = runtime->rpc_incarnation;
      token.private_state.request = server_request;
#if INFO_REQUEST_HAS_VALUE && INFO_RESPONSE_HAS_VALUE
      if (runtime->get_info.value_handler != NULL) {
        int32_t status;
        device_runtime_result_t completed;
        if (runtime->get_info.request_value == NULL || runtime->get_info.response_value == NULL) {
          (void)wl_rpc_server_abandon(runtime->rpc_server, &server_request);
          result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
          break;
        }
        /* The view was decoded and has not crossed an application callback. */
        info_request_wlc_detail_value_copy(
            runtime->get_info.request_scratch, runtime->get_info.request_value);
        info_response_value_clear(runtime->get_info.response_value);
        status = runtime->get_info.value_handler(runtime->get_info.value_user_data,
            runtime->get_info.request_value, runtime->get_info.response_value);
        completed = status == 0
            ? device_get_info_server_complete_value(runtime, &token, runtime->get_info.response_value, now_ms)
            : device_get_info_server_reject(runtime, &token, status, now_ms);
        result.domain = completed.domain;
        result.detail.rpc = completed.detail.rpc;
        if (!device_runtime_result_ok(&completed))
          (void)wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        break;
      }
#endif
      if (runtime->get_info.request_handler == NULL) {
        result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        result.domain = DEVICE_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.rpc.application_result = runtime->get_info.request_handler(
          runtime->get_info.user_data, runtime->get_info.request_scratch, &token, WL_DELIVERY_RELIABLE);
      if (result.detail.rpc.application_result != 0) {
        result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, &server_request);
        result.domain = DEVICE_RUNTIME_APPLICATION_ERROR;
      } else result.domain = DEVICE_RUNTIME_OK;
      break;
    }
    case 4U: {
      wl_rpc_client_result_t client;
      uint64_t session = 0U;
      result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
      if (event->type != WL_EVT_RELIABLE_RX) {
        result.domain = DEVICE_RUNTIME_DELIVERY_MISMATCH;
        break;
      }
      if (runtime->rpc_client == NULL) {
        result.domain = DEVICE_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.rpc.rpc_result = device_rpc_header_read(event->payload,
          event->payload_len, 2U, &result.detail.rpc.operation_id,
          &result.detail.rpc.application_result, &session);
      if (result.detail.rpc.rpc_result != WL_RPC_OK ||
          (result.detail.rpc.application_result != 0 && event->payload_len != 20U)) {
        result.detail.rpc.rpc_result = WL_RPC_ERR_MALFORMED_METADATA;
        result.domain = DEVICE_RUNTIME_RPC_ERROR;
        break;
      }
      if (session != wl_link_session_id(ctx)) {
        /* Observable stale/misdirected reply; do not touch another call. */
        result.detail.rpc.rpc_result = WL_RPC_ERR_SESSION_MISMATCH;
        result.domain = DEVICE_RUNTIME_OK;
        break;
      }
      if (runtime->rpc_async != NULL) {
        uint16_t expired;
        /* A reply received at/after the acceptance deadline cannot resurrect
         * that call, even though event dispatch precedes owner progress. */
        (void)wl_rpc_client_poll(runtime->rpc_client, now_ms, &expired);
      }
      result.detail.rpc.rpc_result = wl_rpc_client_get(runtime->rpc_client,
          result.detail.rpc.operation_id, &client);
      if (result.detail.rpc.rpc_result == WL_RPC_ERR_NOT_FOUND) {
        /* Unknown/late responses are observable, not failures of another call. */
        result.domain = DEVICE_RUNTIME_OK;
        break;
      }
      if (result.detail.rpc.rpc_result != WL_RPC_OK ||
          client.request_message_id != 2U || client.response_message_id != 4U) {
        result.detail.rpc.rpc_result = WL_RPC_ERR_RESPONSE_MISMATCH;
        result.domain = DEVICE_RUNTIME_RPC_ERROR;
        break;
      }
      if (client.state != WL_RPC_CLIENT_LINK_PENDING && client.state != WL_RPC_CLIENT_WAIT_RESPONSE) {
        result.detail.rpc.rpc_result = WL_RPC_ERR_INVALID_STATE;
        result.domain = DEVICE_RUNTIME_OK;
        break;
      }
      if (result.detail.rpc.application_result == 0) {
        if (runtime->get_info.response_scratch == NULL) {
          result.domain = DEVICE_RUNTIME_MISSING_SCRATCH;
          break;
        }
        result.detail.rpc.codec_status = info_response_decode(event->payload + 20U,
            event->payload_len - 20U, runtime->get_info.response_scratch);
        if (result.detail.rpc.codec_status != WL_CODEC_OK) {
          result.domain = DEVICE_RUNTIME_CODEC_ERROR;
          break;
        }
      }
      result.detail.rpc.payload_length = event->payload_len;
      result.detail.rpc.rpc_result = wl_rpc_client_on_response(runtime->rpc_client,
          4U, result.detail.rpc.operation_id, result.detail.rpc.application_result,
          event->payload, event->payload_len);
      result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ?
          DEVICE_RUNTIME_OK : DEVICE_RUNTIME_RPC_ERROR;
      break;
    }
    default:
      result.domain = DEVICE_RUNTIME_UNKNOWN_MESSAGE;
      break;
  }

release_event:
  wl_event_release(ctx, event);
  result.event_consumed = 1U;
  return result;
}

device_runtime_result_t device_configure_client_start(wl_ctx_t *ctx,
    device_runtime_t *runtime, const configure_request_t *request, uint32_t timeout_ms,
    wl_time_ms_t now_ms) {
  device_runtime_result_t result = device_runtime_result(NULL);
  wl_tx_payload_claim_t claim = {0};
  uint32_t operation_id = 0U;
  size_t encoded_length = 0U;
  result.message_id = 5U;
  result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  if (ctx == NULL || runtime == NULL || runtime->rpc_client == NULL || request == NULL)
    return result;
  result.detail.rpc.rpc_result = wl_rpc_client_begin(runtime->rpc_client,
      5U, 6U, timeout_ms, now_ms, &operation_id);
  if (result.detail.rpc.rpc_result != WL_RPC_OK) {
    result.domain = DEVICE_RUNTIME_RPC_ERROR;
    return result;
  }
  result.detail.rpc.operation_id = operation_id;
  result.detail.rpc.core_result = wl_tx_payload_claim(ctx, 5U, WL_DELIVERY_RELIABLE, &claim);
  if (result.detail.rpc.core_result != WL_OK) {
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    goto start_failed;
  }
  if (claim.span.length < 20U) {
    result.detail.rpc.core_result = WL_ERR_BUF_TOO_SMALL;
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    goto start_failed;
  }
  /* Encode directly into the link claim: no whole-request copy or heap. */
  device_rpc_header_write(claim.span.data, 1U, operation_id, 0, wl_link_session_id(ctx));
  result.detail.rpc.codec_status = configure_request_encode(request, claim.span.data + 20U,
      claim.span.length - 20U, &encoded_length);
  if (result.detail.rpc.codec_status != WL_CODEC_OK) {
    result.detail.rpc.core_result = WL_ERR_CORRUPT_PAYLOAD;
    result.domain = DEVICE_RUNTIME_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    goto start_failed;
  }
  result.detail.rpc.payload_length = encoded_length + 20U;
  result.detail.rpc.core_result = wl_tx_payload_commit(ctx, &claim,
      encoded_length + 20U, now_ms, &result.detail.rpc.handle);
  if (result.detail.rpc.core_result != WL_OK) {
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    goto start_failed;
  }
  result.detail.rpc.rpc_result = wl_rpc_client_bind_tx(runtime->rpc_client, operation_id, result.detail.rpc.handle);
  result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_RPC_ERROR;
  return result;
start_failed:
  result.detail.rpc.rpc_result = wl_rpc_client_link_failed(runtime->rpc_client,
      operation_id, result.detail.rpc.core_result);
  if (result.detail.rpc.rpc_result == WL_RPC_OK)
    result.detail.rpc.rpc_result = wl_rpc_client_release(runtime->rpc_client, operation_id);
  if (result.detail.rpc.rpc_result == WL_RPC_OK) result.detail.rpc.operation_id = 0U;
  return result;
}

wl_rpc_err_t device_configure_client_inspect(const device_runtime_t *runtime,
    uint32_t operation_id, wl_rpc_client_result_t *out_client) {
  wl_rpc_err_t result;
  if (out_client != NULL) memset(out_client, 0, sizeof(*out_client));
  if (runtime == NULL || runtime->rpc_client == NULL || out_client == NULL)
    return WL_RPC_ERR_INVALID_ARG;
  result = wl_rpc_client_get(runtime->rpc_client, operation_id, out_client);
  if (result != WL_RPC_OK) return result;
  return out_client->request_message_id == 5U && out_client->response_message_id == 6U
      ? WL_RPC_OK : WL_RPC_ERR_RESPONSE_MISMATCH;
}

device_runtime_result_t device_configure_client_decode(const wl_rpc_client_result_t *client,
    configure_response_t *response) {
  device_runtime_result_t result = device_runtime_result(NULL);
  uint32_t operation_id;
  uint64_t session;
  int32_t status;
  result.message_id = 6U;
  result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  if (client == NULL || response == NULL) return result;
  result.detail.rpc.operation_id = client->operation_id;
  result.detail.rpc.application_result = client->application_status;
  result.detail.rpc.core_result = client->link_result;
  result.detail.rpc.payload_length = client->response_length;
  if (client->request_message_id != 5U || client->response_message_id != 6U) {
    result.detail.rpc.rpc_result = WL_RPC_ERR_RESPONSE_MISMATCH;
    result.domain = DEVICE_RUNTIME_RPC_ERROR;
    return result;
  }
  if (client->state != WL_RPC_CLIENT_COMPLETED) {
    result.detail.rpc.rpc_result = WL_RPC_ERR_INVALID_STATE;
    result.domain = DEVICE_RUNTIME_RPC_ERROR;
    return result;
  }
  result.detail.rpc.rpc_result = device_rpc_header_read(client->response_data,
      client->response_length, 2U, &operation_id, &status, &session);
  if (result.detail.rpc.rpc_result != WL_RPC_OK ||
      operation_id != client->operation_id || status != 0) {
    result.detail.rpc.rpc_result = WL_RPC_ERR_RESPONSE_MISMATCH;
    result.domain = DEVICE_RUNTIME_RPC_ERROR;
    return result;
  }
  result.detail.rpc.codec_status = configure_response_decode(client->response_data + 20U,
      client->response_length - 20U, response);
  result.domain = result.detail.rpc.codec_status == WL_CODEC_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_CODEC_ERROR;
  return result;
}

wl_rpc_err_t device_configure_client_release(device_runtime_t *runtime, uint32_t operation_id) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t result = device_configure_client_inspect(runtime, operation_id, &client);
  return result == WL_RPC_OK ? wl_rpc_client_release(runtime->rpc_client, operation_id) : result;
}

static wl_codec_status_t device_configure_encode_response(const void *response,
    uint8_t *out, size_t capacity, size_t *length, bool owned) {
#if CONFIGURE_REQUEST_HAS_VALUE && CONFIGURE_RESPONSE_HAS_VALUE
  if (owned) return configure_response_value_encode(response, out, capacity, length);
#else
  (void)owned;
#endif
  return configure_response_encode(response, out, capacity, length);
}

static device_runtime_result_t device_configure_server_finish(device_runtime_t *runtime,
    const device_configure_request_token_t *token, int32_t application_status,
    const void *response, wl_time_ms_t now_ms, bool owned) {
  device_runtime_result_t result = device_runtime_result(NULL);
  const wl_rpc_server_request_t *request;
  result.message_id = 6U;
  result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  result.detail.rpc.application_result = application_status;
  if (runtime == NULL || runtime->rpc_server == NULL || token == NULL ||
      token->private_state.owner != runtime ||
      token->private_state.incarnation != runtime->rpc_incarnation ||
      (application_status == 0 && response == NULL)) return result;
  request = &token->private_state.request;
  if (request->identity.request_message_id != 5U ||
      request->identity.response_message_id != 6U) return result;
  device_rpc_finish_response(runtime, request, application_status, response, now_ms,
      owned, device_configure_encode_response, &result);
  return result;
}

wl_rpc_err_t device_configure_request_inspect(device_runtime_t *runtime,
    const device_configure_request_token_t *token, wl_rpc_server_request_t *out_request) {
  if (runtime == NULL || token == NULL || out_request == NULL)
    return WL_RPC_ERR_INVALID_ARG;
  if (token->private_state.owner != runtime ||
      token->private_state.incarnation != runtime->rpc_incarnation ||
      token->private_state.request.identity.request_message_id != 5U ||
      token->private_state.request.identity.response_message_id != 6U)
    return WL_RPC_ERR_NOT_FOUND;
  *out_request = token->private_state.request;
  return WL_RPC_OK;
}

device_runtime_result_t device_configure_server_complete(device_runtime_t *runtime,
    const device_configure_request_token_t *token, const configure_response_t *response, wl_time_ms_t now_ms) {
  return device_configure_server_finish(runtime, token, 0, response, now_ms, false);
}
device_runtime_result_t device_configure_server_reject(device_runtime_t *runtime,
    const device_configure_request_token_t *token, int32_t application_status, wl_time_ms_t now_ms) {
  return device_configure_server_finish(runtime, token, application_status, NULL, now_ms, false);
}

#if CONFIGURE_REQUEST_HAS_VALUE && CONFIGURE_RESPONSE_HAS_VALUE
device_runtime_result_t device_configure_server_complete_value(device_runtime_t *runtime,
    const device_configure_request_token_t *token, const configure_response_value_t *response,
    wl_time_ms_t now_ms) {
  return device_configure_server_finish(runtime, token, 0, response, now_ms, true);
}

wl_err_t device_configure_encode_submission(const void *request, uint32_t operation_id, uint64_t session,
    uint8_t *out, size_t capacity, size_t *length) {
  wl_codec_status_t status;
  size_t size = 0U;
  if (capacity < 20U) return WL_ERR_BUF_TOO_SMALL;
  if (session == 0U) return WL_ERR_INVALID_ARG;
  device_rpc_header_write(out, 1U, operation_id, 0, session);
  status = configure_request_value_encode(request, out + 20U, capacity - 20U, &size);
  if (status != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  *length = size + 20U;
  return WL_OK;
}
#endif

device_runtime_result_t device_get_info_client_start(wl_ctx_t *ctx,
    device_runtime_t *runtime, const info_request_t *request, uint32_t timeout_ms,
    wl_time_ms_t now_ms) {
  device_runtime_result_t result = device_runtime_result(NULL);
  wl_tx_payload_claim_t claim = {0};
  uint32_t operation_id = 0U;
  size_t encoded_length = 0U;
  result.message_id = 2U;
  result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  if (ctx == NULL || runtime == NULL || runtime->rpc_client == NULL || request == NULL)
    return result;
  result.detail.rpc.rpc_result = wl_rpc_client_begin(runtime->rpc_client,
      2U, 4U, timeout_ms, now_ms, &operation_id);
  if (result.detail.rpc.rpc_result != WL_RPC_OK) {
    result.domain = DEVICE_RUNTIME_RPC_ERROR;
    return result;
  }
  result.detail.rpc.operation_id = operation_id;
  result.detail.rpc.core_result = wl_tx_payload_claim(ctx, 2U, WL_DELIVERY_RELIABLE, &claim);
  if (result.detail.rpc.core_result != WL_OK) {
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    goto start_failed;
  }
  if (claim.span.length < 20U) {
    result.detail.rpc.core_result = WL_ERR_BUF_TOO_SMALL;
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    goto start_failed;
  }
  /* Encode directly into the link claim: no whole-request copy or heap. */
  device_rpc_header_write(claim.span.data, 1U, operation_id, 0, wl_link_session_id(ctx));
  result.detail.rpc.codec_status = info_request_encode(request, claim.span.data + 20U,
      claim.span.length - 20U, &encoded_length);
  if (result.detail.rpc.codec_status != WL_CODEC_OK) {
    result.detail.rpc.core_result = WL_ERR_CORRUPT_PAYLOAD;
    result.domain = DEVICE_RUNTIME_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    goto start_failed;
  }
  result.detail.rpc.payload_length = encoded_length + 20U;
  result.detail.rpc.core_result = wl_tx_payload_commit(ctx, &claim,
      encoded_length + 20U, now_ms, &result.detail.rpc.handle);
  if (result.detail.rpc.core_result != WL_OK) {
    result.domain = DEVICE_RUNTIME_CORE_ERROR;
    goto start_failed;
  }
  result.detail.rpc.rpc_result = wl_rpc_client_bind_tx(runtime->rpc_client, operation_id, result.detail.rpc.handle);
  result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_RPC_ERROR;
  return result;
start_failed:
  result.detail.rpc.rpc_result = wl_rpc_client_link_failed(runtime->rpc_client,
      operation_id, result.detail.rpc.core_result);
  if (result.detail.rpc.rpc_result == WL_RPC_OK)
    result.detail.rpc.rpc_result = wl_rpc_client_release(runtime->rpc_client, operation_id);
  if (result.detail.rpc.rpc_result == WL_RPC_OK) result.detail.rpc.operation_id = 0U;
  return result;
}

wl_rpc_err_t device_get_info_client_inspect(const device_runtime_t *runtime,
    uint32_t operation_id, wl_rpc_client_result_t *out_client) {
  wl_rpc_err_t result;
  if (out_client != NULL) memset(out_client, 0, sizeof(*out_client));
  if (runtime == NULL || runtime->rpc_client == NULL || out_client == NULL)
    return WL_RPC_ERR_INVALID_ARG;
  result = wl_rpc_client_get(runtime->rpc_client, operation_id, out_client);
  if (result != WL_RPC_OK) return result;
  return out_client->request_message_id == 2U && out_client->response_message_id == 4U
      ? WL_RPC_OK : WL_RPC_ERR_RESPONSE_MISMATCH;
}

device_runtime_result_t device_get_info_client_decode(const wl_rpc_client_result_t *client,
    info_response_t *response) {
  device_runtime_result_t result = device_runtime_result(NULL);
  uint32_t operation_id;
  uint64_t session;
  int32_t status;
  result.message_id = 4U;
  result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  if (client == NULL || response == NULL) return result;
  result.detail.rpc.operation_id = client->operation_id;
  result.detail.rpc.application_result = client->application_status;
  result.detail.rpc.core_result = client->link_result;
  result.detail.rpc.payload_length = client->response_length;
  if (client->request_message_id != 2U || client->response_message_id != 4U) {
    result.detail.rpc.rpc_result = WL_RPC_ERR_RESPONSE_MISMATCH;
    result.domain = DEVICE_RUNTIME_RPC_ERROR;
    return result;
  }
  if (client->state != WL_RPC_CLIENT_COMPLETED) {
    result.detail.rpc.rpc_result = WL_RPC_ERR_INVALID_STATE;
    result.domain = DEVICE_RUNTIME_RPC_ERROR;
    return result;
  }
  result.detail.rpc.rpc_result = device_rpc_header_read(client->response_data,
      client->response_length, 2U, &operation_id, &status, &session);
  if (result.detail.rpc.rpc_result != WL_RPC_OK ||
      operation_id != client->operation_id || status != 0) {
    result.detail.rpc.rpc_result = WL_RPC_ERR_RESPONSE_MISMATCH;
    result.domain = DEVICE_RUNTIME_RPC_ERROR;
    return result;
  }
  result.detail.rpc.codec_status = info_response_decode(client->response_data + 20U,
      client->response_length - 20U, response);
  result.domain = result.detail.rpc.codec_status == WL_CODEC_OK ? DEVICE_RUNTIME_OK : DEVICE_RUNTIME_CODEC_ERROR;
  return result;
}

wl_rpc_err_t device_get_info_client_release(device_runtime_t *runtime, uint32_t operation_id) {
  wl_rpc_client_result_t client;
  wl_rpc_err_t result = device_get_info_client_inspect(runtime, operation_id, &client);
  return result == WL_RPC_OK ? wl_rpc_client_release(runtime->rpc_client, operation_id) : result;
}

static wl_codec_status_t device_get_info_encode_response(const void *response,
    uint8_t *out, size_t capacity, size_t *length, bool owned) {
#if INFO_REQUEST_HAS_VALUE && INFO_RESPONSE_HAS_VALUE
  if (owned) return info_response_value_encode(response, out, capacity, length);
#else
  (void)owned;
#endif
  return info_response_encode(response, out, capacity, length);
}

static device_runtime_result_t device_get_info_server_finish(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, int32_t application_status,
    const void *response, wl_time_ms_t now_ms, bool owned) {
  device_runtime_result_t result = device_runtime_result(NULL);
  const wl_rpc_server_request_t *request;
  result.message_id = 4U;
  result.detail_kind = DEVICE_RUNTIME_DETAIL_RPC;
  result.detail.rpc.application_result = application_status;
  if (runtime == NULL || runtime->rpc_server == NULL || token == NULL ||
      token->private_state.owner != runtime ||
      token->private_state.incarnation != runtime->rpc_incarnation ||
      (application_status == 0 && response == NULL)) return result;
  request = &token->private_state.request;
  if (request->identity.request_message_id != 2U ||
      request->identity.response_message_id != 4U) return result;
  device_rpc_finish_response(runtime, request, application_status, response, now_ms,
      owned, device_get_info_encode_response, &result);
  return result;
}

wl_rpc_err_t device_get_info_request_inspect(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, wl_rpc_server_request_t *out_request) {
  if (runtime == NULL || token == NULL || out_request == NULL)
    return WL_RPC_ERR_INVALID_ARG;
  if (token->private_state.owner != runtime ||
      token->private_state.incarnation != runtime->rpc_incarnation ||
      token->private_state.request.identity.request_message_id != 2U ||
      token->private_state.request.identity.response_message_id != 4U)
    return WL_RPC_ERR_NOT_FOUND;
  *out_request = token->private_state.request;
  return WL_RPC_OK;
}

device_runtime_result_t device_get_info_server_complete(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, const info_response_t *response, wl_time_ms_t now_ms) {
  return device_get_info_server_finish(runtime, token, 0, response, now_ms, false);
}
device_runtime_result_t device_get_info_server_reject(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, int32_t application_status, wl_time_ms_t now_ms) {
  return device_get_info_server_finish(runtime, token, application_status, NULL, now_ms, false);
}

#if INFO_REQUEST_HAS_VALUE && INFO_RESPONSE_HAS_VALUE
device_runtime_result_t device_get_info_server_complete_value(device_runtime_t *runtime,
    const device_get_info_request_token_t *token, const info_response_value_t *response,
    wl_time_ms_t now_ms) {
  return device_get_info_server_finish(runtime, token, 0, response, now_ms, true);
}

wl_err_t device_get_info_encode_submission(const void *request, uint32_t operation_id, uint64_t session,
    uint8_t *out, size_t capacity, size_t *length) {
  wl_codec_status_t status;
  size_t size = 0U;
  if (capacity < 20U) return WL_ERR_BUF_TOO_SMALL;
  if (session == 0U) return WL_ERR_INVALID_ARG;
  device_rpc_header_write(out, 1U, operation_id, 0, session);
  status = info_request_value_encode(request, out + 20U, capacity - 20U, &size);
  if (status != WL_CODEC_OK) return WL_ERR_CORRUPT_PAYLOAD;
  *length = size + 20U;
  return WL_OK;
}
#endif

static wl_pump_event_disposition_t device_runtime_pump_event(void *user_data, wl_ctx_t *ctx, const wl_event_t *event, wl_time_ms_t now_ms) {
  device_runtime_pump_t *pump = (device_runtime_pump_t *)user_data;
  device_runtime_result_t result;
  if (pump == NULL || pump->runtime == NULL) return WL_PUMP_EVENT_UNHANDLED;
  result = device_runtime_dispatch_event(ctx, event, pump->runtime, now_ms);
  if (pump->on_result != NULL) pump->on_result(pump->user_data, &result);
  return result.event_consumed != 0U ? WL_PUMP_EVENT_CONSUMED : WL_PUMP_EVENT_UNHANDLED;
}

static uint8_t device_runtime_pump_progress(void *user_data, wl_ctx_t *ctx,
    wl_time_ms_t now_ms) {
  device_runtime_pump_t *pump = (device_runtime_pump_t *)user_data;
  uint8_t progress = 0U;
  if (pump == NULL || pump->runtime == NULL) return 0U;
  pump->last_service_result = device_runtime_service(ctx, pump->runtime, now_ms, &pump->last_service);
  if (pump->last_service_result == WL_RPC_OK) {
    if (pump->last_service.response.message_id != 0U && pump->on_result != NULL)
      pump->on_result(pump->user_data, &pump->last_service.response);
    progress = pump->last_service.responses_submitted != 0U ? 1U : 0U;
  }
  if (pump->runtime->rpc_async != NULL) {
    uint16_t notified = 0U;
    if (wl_rpc_async_service(pump->runtime->rpc_async, now_ms, &notified) != WL_OK)
      pump->last_service_result = WL_RPC_ERR_INVALID_STATE;
    if (notified != 0U) progress = 1U;
  }
  return progress;
}

static uint32_t device_runtime_pump_deadline(const void *user_data, wl_time_ms_t now_ms) {
  const device_runtime_pump_t *pump = (const device_runtime_pump_t *)user_data;
  wl_rpc_deadline_hint_t hint = {0};
  if (pump == NULL || pump->runtime == NULL ||
      device_runtime_get_deadline_hint(pump->runtime, now_ms, &hint) != WL_RPC_OK)
    return WL_POLL_NO_DEADLINE_MS;
  if (pump->runtime->rpc_async != NULL &&
      wl_rpc_async_notification_deadline(pump->runtime->rpc_async) == 0U) return 0U;
  return hint.next_deadline_ms;
}

wl_err_t device_runtime_pump_init(device_runtime_pump_t *pump, device_runtime_t *runtime, device_runtime_result_fn on_result, void *user_data) {
  if (pump == NULL || runtime == NULL) return WL_ERR_INVALID_ARG;
  memset(pump, 0, sizeof(*pump));
  pump->runtime = runtime;
  pump->user_data = user_data;
  pump->on_result = on_result;
  return WL_OK;
}

wl_pump_hooks_t device_runtime_pump_hooks(device_runtime_pump_t *pump) {
  wl_pump_hooks_t hooks = {0};
  if (pump == NULL) return hooks;
  hooks.application_user_data = pump;
  hooks.on_event = device_runtime_pump_event;
  hooks.application_progress = device_runtime_pump_progress;
  hooks.application_deadline_hint = device_runtime_pump_deadline;
  return hooks;
}
