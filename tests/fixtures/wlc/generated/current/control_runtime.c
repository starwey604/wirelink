#include "control_runtime.h"

#include <string.h>

static control_runtime_result_t control_runtime_result(const wl_event_t *event) {
  control_runtime_result_t result = {0};
  result.domain = CONTROL_RUNTIME_INVALID_ARGUMENT;
  if (event != NULL) {
    result.message_id = event->message_id;
    result.event_type = event->type;
  }
  return result;
}

static uint64_t control_rpc_request_fingerprint(const uint8_t *data, size_t length) {
  static const uint8_t domain[] = "wlc.rpc.canonical-request.v1";
  uint64_t hash = UINT64_C(0xcbf29ce484222325);
  size_t index;
  for (index = 0U; index + 1U < sizeof(domain); ++index) {
    hash ^= (uint64_t)domain[index];
    hash *= UINT64_C(0x00000100000001b3);
  }
  hash ^= UINT64_C(0xff);
  hash *= UINT64_C(0x00000100000001b3);
  for (index = 0U; index < length; ++index) {
    hash ^= (uint64_t)data[index];
    hash *= UINT64_C(0x00000100000001b3);
  }
  return hash;
}

typedef struct {
  uint8_t *base;
  size_t size;
  size_t offset;
} control_runtime_storage_cursor_t;

typedef struct {
  void *joint_command_fifo_storage;
  void *arm_mit_command_latest_storage;
  void *rpc_client_slots;
  void *rpc_client_responses;
  size_t rpc_client_responses_size;
  void *rpc_server_pending_slots;
  void *rpc_server_cache_slots;
  void *rpc_server_responses;
  size_t rpc_server_responses_size;
  void *home_canonical_request_storage;
} control_runtime_layout_t;

static int control_runtime_storage_region(control_runtime_storage_cursor_t *cursor, size_t alignment, size_t count, size_t element_size, void **out_data, size_t *out_size) {
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

static int control_runtime_layout(const control_runtime_config_t *config, uint8_t *base, size_t size, control_runtime_layout_t *out_layout, control_runtime_requirements_t *out_requirements) {
  control_runtime_storage_cursor_t cursor = {base, size, 0U};
  size_t alignment = 1U;
  int result;
  if (out_layout != NULL) memset(out_layout, 0, sizeof(*out_layout));
  if (out_requirements != NULL) memset(out_requirements, 0, sizeof(*out_requirements));
  if (config == NULL) return WL_ERR_INVALID_ARG;
  {
    const wl_fifo_config_t route_config = {sizeof(joint_command_t), _Alignof(joint_command_t), config->joint_command_fifo_capacity};
    wl_fifo_requirements_t route_requirements;
    result = wl_fifo_requirements(&route_config, &route_requirements);
    if (result != WL_OK) return result;
    if (alignment < _Alignof(joint_command_t)) alignment = _Alignof(joint_command_t);
    result = control_runtime_storage_region(&cursor, _Alignof(joint_command_t), 1U, route_requirements.storage_size, out_layout == NULL ? NULL : &out_layout->joint_command_fifo_storage, NULL);
    if (result != WL_OK) return result;
  }
  {
    const wl_latest_config_t route_config = {sizeof(arm_mit_command_t), _Alignof(arm_mit_command_t), config->arm_mit_command_latest_initial_generation};
    wl_latest_requirements_t route_requirements;
    result = wl_latest_requirements(&route_config, &route_requirements);
    if (result != WL_OK) return result;
    if (alignment < _Alignof(arm_mit_command_t)) alignment = _Alignof(arm_mit_command_t);
    result = control_runtime_storage_region(&cursor, _Alignof(arm_mit_command_t), 1U, route_requirements.storage_size, out_layout == NULL ? NULL : &out_layout->arm_mit_command_latest_storage, NULL);
    if (result != WL_OK) return result;
  }
  if (config->rpc_client_enabled > 1U || config->rpc_server_enabled > 1U) return WL_ERR_INVALID_ARG;
  if (config->rpc_client_enabled != 0U) {
    if (config->rpc_client_slot_count == 0U || config->rpc_client_response_capacity == 0U) return WL_ERR_INVALID_ARG;
    if (alignment < _Alignof(wl_rpc_client_slot_t)) alignment = _Alignof(wl_rpc_client_slot_t);
    result = control_runtime_storage_region(&cursor, _Alignof(wl_rpc_client_slot_t), config->rpc_client_slot_count, sizeof(wl_rpc_client_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_client_slots, NULL);
    if (result != WL_OK) return result;
    result = control_runtime_storage_region(&cursor, 1U, config->rpc_client_slot_count, config->rpc_client_response_capacity, out_layout == NULL ? NULL : &out_layout->rpc_client_responses, out_layout == NULL ? NULL : &out_layout->rpc_client_responses_size);
    if (result != WL_OK) return result;
  }
  if (config->rpc_server_enabled != 0U) {
    if (config->rpc_server_pending_slot_count == 0U || config->rpc_server_cache_slot_count == 0U || config->rpc_server_response_capacity == 0U) return WL_ERR_INVALID_ARG;
    if ((config->rpc_server_pending_timeout_ms != 0U && config->rpc_server_pending_timeout_ms >= UINT32_C(0x80000000)) || (config->rpc_server_cache_ttl_ms != 0U && config->rpc_server_cache_ttl_ms >= UINT32_C(0x80000000))) return WL_ERR_INVALID_ARG;
    if (config->rpc_server_cache_policy != WL_RPC_CACHE_REJECT_NEW && config->rpc_server_cache_policy != WL_RPC_CACHE_EVICT_OLDEST) return WL_ERR_INVALID_ARG;
    if (alignment < _Alignof(wl_rpc_server_pending_slot_t)) alignment = _Alignof(wl_rpc_server_pending_slot_t);
    if (alignment < _Alignof(wl_rpc_server_cache_slot_t)) alignment = _Alignof(wl_rpc_server_cache_slot_t);
    result = control_runtime_storage_region(&cursor, _Alignof(wl_rpc_server_pending_slot_t), config->rpc_server_pending_slot_count, sizeof(wl_rpc_server_pending_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_server_pending_slots, NULL);
    if (result != WL_OK) return result;
    result = control_runtime_storage_region(&cursor, _Alignof(wl_rpc_server_cache_slot_t), config->rpc_server_cache_slot_count, sizeof(wl_rpc_server_cache_slot_t), out_layout == NULL ? NULL : &out_layout->rpc_server_cache_slots, NULL);
    if (result != WL_OK) return result;
    result = control_runtime_storage_region(&cursor, 1U, config->rpc_server_cache_slot_count, config->rpc_server_response_capacity, out_layout == NULL ? NULL : &out_layout->rpc_server_responses, out_layout == NULL ? NULL : &out_layout->rpc_server_responses_size);
    if (result != WL_OK) return result;
    if (config->home_canonical_request_capacity == 0U) return WL_ERR_INVALID_ARG;
    result = control_runtime_storage_region(&cursor, 1U, 1U, config->home_canonical_request_capacity, out_layout == NULL ? NULL : &out_layout->home_canonical_request_storage, NULL);
    if (result != WL_OK) return result;
  }
  if (out_requirements != NULL) {
    out_requirements->storage_size = cursor.offset;
    out_requirements->storage_alignment = alignment;
  }
  return WL_OK;
}

int control_runtime_requirements(const control_runtime_config_t *config, control_runtime_requirements_t *out_requirements) {
  control_runtime_config_t config_copy;
  if (config == NULL || out_requirements == NULL) return WL_ERR_INVALID_ARG;
  config_copy = *config;
  *out_requirements = (control_runtime_requirements_t){0};
  return control_runtime_layout(&config_copy, NULL, SIZE_MAX, NULL, out_requirements);
}

int control_runtime_init(control_runtime_instance_t *instance, const control_runtime_config_t *config, const control_runtime_storage_t *storage) {
  control_runtime_config_t config_copy;
  control_runtime_storage_t storage_copy;
  control_runtime_requirements_t requirements;
  control_runtime_layout_t layout;
  uintptr_t instance_address;
  uintptr_t storage_address;
  int result;
  if (instance == NULL || config == NULL || storage == NULL) return WL_ERR_INVALID_ARG;
  config_copy = *config;
  storage_copy = *storage;
  config = &config_copy;
  storage = &storage_copy;
  result = control_runtime_requirements(config, &requirements);
  if (result != WL_OK) return result;
  if (storage->size < requirements.storage_size) return WL_ERR_BUF_TOO_SMALL;
  if (requirements.storage_size != 0U) {
    if (storage->data == NULL || ((uintptr_t)storage->data & (requirements.storage_alignment - 1U)) != 0U) return WL_ERR_INVALID_ARG;
    instance_address = (uintptr_t)(void *)instance;
    storage_address = (uintptr_t)storage->data;
    if ((storage_address <= instance_address && instance_address - storage_address < requirements.storage_size) || (instance_address < storage_address && storage_address - instance_address < sizeof(*instance))) return WL_ERR_INVALID_ARG;
  }
  result = control_runtime_layout(config, (uint8_t *)storage->data, storage->size, &layout, NULL);
  if (result != WL_OK) return result;
  memset(instance, 0, sizeof(*instance));
  {
    const wl_fifo_config_t route_config = {sizeof(joint_command_t), _Alignof(joint_command_t), config->joint_command_fifo_capacity};
    wl_fifo_requirements_t route_requirements;
    wl_fifo_storage_t route_storage;
    result = wl_fifo_requirements(&route_config, &route_requirements);
    if (result != WL_OK) goto init_failed;
    route_storage.data = layout.joint_command_fifo_storage;
    route_storage.size = route_requirements.storage_size;
    result = wl_fifo_init(&instance->joint_command_fifo, &route_config, &route_storage);
    if (result != WL_OK) goto init_failed;
    instance->runtime.joint_command_fifo = &instance->joint_command_fifo;
  }
  {
    const wl_latest_config_t route_config = {sizeof(arm_mit_command_t), _Alignof(arm_mit_command_t), config->arm_mit_command_latest_initial_generation};
    wl_latest_requirements_t route_requirements;
    wl_latest_storage_t route_storage;
    result = wl_latest_requirements(&route_config, &route_requirements);
    if (result != WL_OK) goto init_failed;
    route_storage.data = layout.arm_mit_command_latest_storage;
    route_storage.size = route_requirements.storage_size;
    result = wl_latest_init(&instance->arm_mit_command_latest, &route_config, &route_storage);
    if (result != WL_OK) goto init_failed;
    instance->runtime.arm_mit_command_latest = &instance->arm_mit_command_latest;
  }
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
  if (config->rpc_server_enabled != 0U) {
    instance->runtime.home.request_scratch = &instance->home_request_scratch;
    instance->runtime.home.canonical_request_scratch.data = (uint8_t *)layout.home_canonical_request_storage;
    instance->runtime.home.canonical_request_scratch.capacity = config->home_canonical_request_capacity;
    instance->runtime.home.request_handler = config->home_request_handler;
    instance->runtime.home.user_data = config->home_user_data;
  }
  if (config->rpc_client_enabled != 0U) instance->runtime.home.response_scratch = &instance->home_response_scratch;
  return WL_OK;

init_failed:
  memset(instance, 0, sizeof(*instance));
  return result;
}

control_runtime_result_t control_runtime_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, control_runtime_t *runtime, wl_time_ms_t now_ms) {
  control_runtime_result_t result = control_runtime_result(event);
  if (event == NULL) return result;
  (void)now_ms;
  if (event->type == WL_EVT_TX_SUCCESS || event->type == WL_EVT_TX_TIMEOUT || event->type == WL_EVT_TX_FAILED) {
    if (runtime == NULL || runtime->rpc_client == NULL) {
      result.domain = CONTROL_RUNTIME_NON_RX;
      return result;
    }
    result.detail_kind = CONTROL_RUNTIME_DETAIL_RPC;
    result.detail.rpc.rpc_result = wl_rpc_client_on_tx_event(runtime->rpc_client, event);
    if (result.detail.rpc.rpc_result == WL_RPC_OK) result.domain = CONTROL_RUNTIME_OK;
    else if (result.detail.rpc.rpc_result == WL_RPC_ERR_NOT_FOUND) result.domain = CONTROL_RUNTIME_NON_RX;
    else result.domain = CONTROL_RUNTIME_RPC_ERROR;
    return result;
  }
  if (event->type != WL_EVT_UNRELIABLE_RX && event->type != WL_EVT_RELIABLE_RX) {
    result.domain = CONTROL_RUNTIME_NON_RX;
    return result;
  }
  if (ctx == NULL) return result;
  if (runtime == NULL) goto release_event;

  switch (event->message_id) {
    case JOINT_COMMAND_MESSAGE_ID: {
      wl_fifo_write_claim_t claim = {0};
      result.detail_kind = CONTROL_RUNTIME_DETAIL_RETAINED;
      if (event->type != WL_EVT_RELIABLE_RX) {
        result.domain = CONTROL_RUNTIME_DELIVERY_MISMATCH;
        break;
      }
      if (runtime->joint_command_fifo == NULL) {
        result.domain = CONTROL_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.retained.storage_result = wl_fifo_write_claim(runtime->joint_command_fifo, &claim);
      if (result.detail.retained.storage_result != WL_OK) {
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      if (claim.value_size < sizeof(joint_command_t)) {
        result.detail.retained.storage_result = WL_ERR_BUF_TOO_SMALL;
        result.detail.retained.abort_result = wl_fifo_write_abort(runtime->joint_command_fifo, &claim);
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      if (((uintptr_t)claim.value % _Alignof(joint_command_t)) != 0U) {
        result.detail.retained.storage_result = WL_ERR_INVALID_ARG;
        result.detail.retained.abort_result = wl_fifo_write_abort(runtime->joint_command_fifo, &claim);
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      result.detail.retained.codec_status = joint_command_decode(event->payload, event->payload_len, (joint_command_t *)claim.value);
      if (result.detail.retained.codec_status != WL_CODEC_OK) {
        result.detail.retained.abort_result = wl_fifo_write_abort(runtime->joint_command_fifo, &claim);
        result.domain = CONTROL_RUNTIME_CODEC_ERROR;
        break;
      }
      result.detail.retained.storage_result = wl_fifo_write_publish(runtime->joint_command_fifo, &claim);
      if (result.detail.retained.storage_result != WL_OK) {
        result.detail.retained.abort_result = wl_fifo_write_abort(runtime->joint_command_fifo, &claim);
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      result.domain = CONTROL_RUNTIME_OK;
      break;
    }
    case ARM_MIT_COMMAND_MESSAGE_ID: {
      wl_latest_write_claim_t claim = {0};
      result.detail_kind = CONTROL_RUNTIME_DETAIL_RETAINED;
      if (event->type != WL_EVT_UNRELIABLE_RX) {
        result.domain = CONTROL_RUNTIME_DELIVERY_MISMATCH;
        break;
      }
      if (runtime->arm_mit_command_latest == NULL) {
        result.domain = CONTROL_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.detail.retained.storage_result = wl_latest_write_claim(runtime->arm_mit_command_latest, &claim);
      if (result.detail.retained.storage_result != WL_OK) {
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      if (claim.value_size < sizeof(arm_mit_command_t)) {
        result.detail.retained.storage_result = WL_ERR_BUF_TOO_SMALL;
        result.detail.retained.abort_result = wl_latest_write_abort(runtime->arm_mit_command_latest, &claim);
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      if (((uintptr_t)claim.value % _Alignof(arm_mit_command_t)) != 0U) {
        result.detail.retained.storage_result = WL_ERR_INVALID_ARG;
        result.detail.retained.abort_result = wl_latest_write_abort(runtime->arm_mit_command_latest, &claim);
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      result.detail.retained.codec_status = arm_mit_command_decode(event->payload, event->payload_len, (arm_mit_command_t *)claim.value);
      if (result.detail.retained.codec_status != WL_CODEC_OK) {
        result.detail.retained.abort_result = wl_latest_write_abort(runtime->arm_mit_command_latest, &claim);
        result.domain = CONTROL_RUNTIME_CODEC_ERROR;
        break;
      }
      result.detail.retained.storage_result = wl_latest_write_publish(runtime->arm_mit_command_latest, &claim);
      if (result.detail.retained.storage_result != WL_OK) {
        result.detail.retained.abort_result = wl_latest_write_abort(runtime->arm_mit_command_latest, &claim);
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      result.domain = CONTROL_RUNTIME_OK;
      break;
    }
    case HOME_REQUEST_MESSAGE_ID: {
      wl_rpc_request_identity_t identity = {0};
      wl_rpc_server_response_t replay = {0};
      control_runtime_result_t retry;
      size_t canonical_length = 0U;
      result.detail_kind = CONTROL_RUNTIME_DETAIL_RPC;
      if (event->type != WL_EVT_RELIABLE_RX) {
        result.domain = CONTROL_RUNTIME_DELIVERY_MISMATCH;
        break;
      }
      if (runtime->rpc_server == NULL) {
        result.domain = CONTROL_RUNTIME_MISSING_ROUTE;
        break;
      }
      if (runtime->home.request_scratch == NULL || runtime->home.canonical_request_scratch.data == NULL) {
        result.domain = CONTROL_RUNTIME_MISSING_SCRATCH;
        break;
      }
      result.detail.rpc.codec_status = home_request_decode(event->payload, event->payload_len, runtime->home.request_scratch);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_RUNTIME_CODEC_ERROR;
        break;
      }
      if (!runtime->home.request_scratch->has_operation_id || runtime->home.request_scratch->operation_id == 0U) {
        result.detail.rpc.rpc_result = WL_RPC_ERR_INVALID_ARG;
        result.domain = CONTROL_RUNTIME_RPC_ERROR;
        break;
      }
      result.detail.rpc.operation_id = runtime->home.request_scratch->operation_id;
      result.detail.rpc.codec_status = home_request_encode(runtime->home.request_scratch, runtime->home.canonical_request_scratch.data, runtime->home.canonical_request_scratch.capacity, &canonical_length);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_RUNTIME_CODEC_ERROR;
        break;
      }
      result.detail.rpc.payload_length = canonical_length;
      identity.operation_id = result.detail.rpc.operation_id;
      identity.request_message_id = HOME_REQUEST_MESSAGE_ID;
      identity.response_message_id = HOME_RESPONSE_MESSAGE_ID;
      identity.request_fingerprint = control_rpc_request_fingerprint(runtime->home.canonical_request_scratch.data, canonical_length);
      result.detail.rpc.rpc_result = wl_rpc_server_begin(runtime->rpc_server, &identity, now_ms, &result.detail.rpc.rpc_disposition, &replay);
      if (result.detail.rpc.rpc_result != WL_RPC_OK) {
        result.domain = CONTROL_RUNTIME_RPC_ERROR;
        break;
      }
      result.detail.rpc.server_response = replay;
      switch (result.detail.rpc.rpc_disposition) {
        case WL_RPC_SERVER_NEW:
          if (runtime->home.request_handler == NULL) {
            result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, result.detail.rpc.operation_id);
            result.domain = CONTROL_RUNTIME_MISSING_ROUTE;
            break;
          }
          result.detail.rpc.application_result = runtime->home.request_handler(runtime->home.user_data, runtime->home.request_scratch, WL_DELIVERY_RELIABLE);
          if (result.detail.rpc.application_result != 0) {
            result.detail.rpc.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, result.detail.rpc.operation_id);
            result.domain = CONTROL_RUNTIME_APPLICATION_ERROR;
          } else {
            result.domain = CONTROL_RUNTIME_OK;
          }
          break;
        case WL_RPC_SERVER_PENDING_DUPLICATE:
          result.domain = CONTROL_RUNTIME_OK;
          break;
        case WL_RPC_SERVER_REPLAY:
          retry = control_home_server_retry_cached(ctx, &replay);
          result.detail.rpc.core_result = retry.detail.rpc.core_result;
          result.detail.rpc.handle = retry.detail.rpc.handle;
          result.detail.rpc.payload_length = retry.detail.rpc.payload_length;
          result.detail.rpc.application_result = retry.detail.rpc.application_result;
          result.domain = retry.domain;
          break;
        case WL_RPC_SERVER_CONFLICT:
          result.detail.rpc.rpc_result = WL_RPC_ERR_OPERATION_CONFLICT;
          result.domain = CONTROL_RUNTIME_RPC_ERROR;
          break;
        default:
          result.detail.rpc.rpc_result = WL_RPC_ERR_INVALID_STATE;
          result.domain = CONTROL_RUNTIME_RPC_ERROR;
          break;
      }
      break;
    }
    case HOME_RESPONSE_MESSAGE_ID: {
      result.detail_kind = CONTROL_RUNTIME_DETAIL_RPC;
      if (event->type != WL_EVT_RELIABLE_RX) {
        result.domain = CONTROL_RUNTIME_DELIVERY_MISMATCH;
        break;
      }
      if (runtime->rpc_client == NULL) {
        result.domain = CONTROL_RUNTIME_MISSING_ROUTE;
        break;
      }
      if (runtime->home.response_scratch == NULL) {
        result.domain = CONTROL_RUNTIME_MISSING_SCRATCH;
        break;
      }
      result.detail.rpc.codec_status = home_response_decode(event->payload, event->payload_len, runtime->home.response_scratch);
      if (result.detail.rpc.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_RUNTIME_CODEC_ERROR;
        break;
      }
      if (!runtime->home.response_scratch->has_operation_id || runtime->home.response_scratch->operation_id == 0U || !runtime->home.response_scratch->has_status) {
        result.detail.rpc.rpc_result = WL_RPC_ERR_RESPONSE_MISMATCH;
        result.domain = CONTROL_RUNTIME_RPC_ERROR;
        break;
      }
      result.detail.rpc.operation_id = runtime->home.response_scratch->operation_id;
      result.detail.rpc.application_result = (int32_t)runtime->home.response_scratch->status;
      result.detail.rpc.payload_length = event->payload_len;
      result.detail.rpc.rpc_result = wl_rpc_client_on_response(runtime->rpc_client, HOME_RESPONSE_MESSAGE_ID, result.detail.rpc.operation_id, result.detail.rpc.application_result, event->payload, event->payload_len);
      result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ? CONTROL_RUNTIME_OK : CONTROL_RUNTIME_RPC_ERROR;
      break;
    }
    default:
      result.domain = CONTROL_RUNTIME_UNKNOWN_MESSAGE;
      break;
  }

release_event:
  wl_event_release(ctx, event);
  return result;
}

static control_runtime_result_t control_home_client_finish_start(control_runtime_t *runtime, uint32_t operation_id, control_send_result_t sent) {
  control_runtime_result_t result = control_runtime_result(NULL);
  result.message_id = HOME_REQUEST_MESSAGE_ID;
  result.detail_kind = CONTROL_RUNTIME_DETAIL_RPC;
  result.detail.rpc.operation_id = operation_id;
  result.detail.rpc.codec_status = sent.codec_status;
  result.detail.rpc.core_result = sent.core_result;
  result.detail.rpc.abort_result = sent.abort_result;
  result.detail.rpc.handle = sent.handle;
  result.detail.rpc.payload_length = sent.payload_length;
  if (sent.domain == CONTROL_SEND_CODEC_ERROR) {
    result.detail.rpc.rpc_result = wl_rpc_client_link_failed(runtime->rpc_client, operation_id, WL_ERR_CORRUPT_PAYLOAD);
    result.domain = CONTROL_RUNTIME_CODEC_ERROR;
    return result;
  }
  if (sent.domain == CONTROL_SEND_CORE_ERROR) {
    result.detail.rpc.rpc_result = wl_rpc_client_link_failed(runtime->rpc_client, operation_id, sent.core_result);
    result.domain = CONTROL_RUNTIME_CORE_ERROR;
    return result;
  }
  result.detail.rpc.rpc_result = wl_rpc_client_bind_tx(runtime->rpc_client, operation_id, result.detail.rpc.handle);
  result.domain = result.detail.rpc.rpc_result == WL_RPC_OK ? CONTROL_RUNTIME_OK : CONTROL_RUNTIME_RPC_ERROR;
  return result;
}

control_runtime_result_t control_home_client_start_scratch(wl_ctx_t *ctx, control_runtime_t *runtime, home_request_t *request, uint32_t timeout_ms, wl_time_ms_t now_ms, control_encode_scratch_t scratch) {
  control_runtime_result_t result = control_runtime_result(NULL);
  control_send_result_t sent;
  uint32_t operation_id = 0U;
  result.message_id = HOME_REQUEST_MESSAGE_ID;
  result.detail_kind = CONTROL_RUNTIME_DETAIL_RPC;
  if (ctx == NULL || runtime == NULL || runtime->rpc_client == NULL || request == NULL) return result;
  result.detail.rpc.rpc_result = wl_rpc_client_begin(runtime->rpc_client, HOME_REQUEST_MESSAGE_ID, HOME_RESPONSE_MESSAGE_ID, timeout_ms, now_ms, &operation_id);
  result.detail.rpc.operation_id = operation_id;
  if (result.detail.rpc.rpc_result != WL_RPC_OK) {
    result.domain = CONTROL_RUNTIME_RPC_ERROR;
    return result;
  }
  request->has_operation_id = true;
  request->operation_id = operation_id;
  sent = control_home_request_send_reliable(ctx, request, scratch);
  return control_home_client_finish_start(runtime, operation_id, sent);
}

control_runtime_result_t control_home_client_start_direct(wl_ctx_t *ctx, control_runtime_t *runtime, home_request_t *request, uint32_t timeout_ms, wl_time_ms_t now_ms) {
  control_runtime_result_t result = control_runtime_result(NULL);
  control_send_result_t sent;
  uint32_t operation_id = 0U;
  result.message_id = HOME_REQUEST_MESSAGE_ID;
  result.detail_kind = CONTROL_RUNTIME_DETAIL_RPC;
  if (ctx == NULL || runtime == NULL || runtime->rpc_client == NULL || request == NULL) return result;
  result.detail.rpc.rpc_result = wl_rpc_client_begin(runtime->rpc_client, HOME_REQUEST_MESSAGE_ID, HOME_RESPONSE_MESSAGE_ID, timeout_ms, now_ms, &operation_id);
  result.detail.rpc.operation_id = operation_id;
  if (result.detail.rpc.rpc_result != WL_RPC_OK) {
    result.domain = CONTROL_RUNTIME_RPC_ERROR;
    return result;
  }
  request->has_operation_id = true;
  request->operation_id = operation_id;
  sent = control_home_request_send_direct(ctx, request, WL_DELIVERY_RELIABLE);
  return control_home_client_finish_start(runtime, operation_id, sent);
}

control_runtime_result_t control_home_server_retry_cached(wl_ctx_t *ctx, const wl_rpc_server_response_t *cached) {
  control_runtime_result_t result = control_runtime_result(NULL);
  result.message_id = HOME_RESPONSE_MESSAGE_ID;
  result.detail_kind = CONTROL_RUNTIME_DETAIL_RPC;
  if (ctx == NULL || cached == NULL || cached->identity.operation_id == 0U || cached->identity.request_message_id != HOME_REQUEST_MESSAGE_ID || cached->identity.response_message_id != HOME_RESPONSE_MESSAGE_ID || (cached->response_length != 0U && cached->response_data == NULL)) return result;
  result.detail.rpc.operation_id = cached->identity.operation_id;
  result.detail.rpc.application_result = cached->application_status;
  result.detail.rpc.payload_length = cached->response_length;
  result.detail.rpc.server_response = *cached;
  result.detail.rpc.core_result = wl_send_reliable(ctx, HOME_RESPONSE_MESSAGE_ID, cached->response_data, cached->response_length, &result.detail.rpc.handle);
  result.domain = result.detail.rpc.core_result == WL_OK ? CONTROL_RUNTIME_OK : CONTROL_RUNTIME_CORE_ERROR;
  return result;
}

static control_runtime_result_t control_home_server_finish(wl_ctx_t *ctx, control_runtime_t *runtime, uint32_t operation_id, int32_t application_status, home_response_t *response, control_encode_scratch_t scratch, wl_time_ms_t now_ms, bool reject) {
  control_runtime_result_t result = control_runtime_result(NULL);
  wl_rpc_server_response_t cached = {0};
  size_t encoded_length = 0U;
  result.message_id = HOME_RESPONSE_MESSAGE_ID;
  result.detail_kind = CONTROL_RUNTIME_DETAIL_RPC;
  result.detail.rpc.operation_id = operation_id;
  result.detail.rpc.application_result = application_status;
  if (ctx == NULL || runtime == NULL || runtime->rpc_server == NULL || operation_id == 0U || response == NULL) return result;
  if (reject && application_status == 0) {
    result.detail.rpc.rpc_result = WL_RPC_ERR_INVALID_ARG;
    result.domain = CONTROL_RUNTIME_RPC_ERROR;
    return result;
  }
  response->has_operation_id = true;
  response->operation_id = operation_id;
  response->has_status = true;
  response->status = application_status;
  result.detail.rpc.codec_status = home_response_encode(response, scratch.data, scratch.capacity, &encoded_length);
  result.detail.rpc.payload_length = encoded_length;
  if (result.detail.rpc.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_RUNTIME_CODEC_ERROR;
    return result;
  }
  if (reject) result.detail.rpc.rpc_result = wl_rpc_server_reject(runtime->rpc_server, operation_id, application_status, scratch.data, encoded_length, now_ms, &cached);
  else result.detail.rpc.rpc_result = wl_rpc_server_complete(runtime->rpc_server, operation_id, application_status, scratch.data, encoded_length, now_ms, &cached);
  if (result.detail.rpc.rpc_result != WL_RPC_OK) {
    result.domain = CONTROL_RUNTIME_RPC_ERROR;
    return result;
  }
  return control_home_server_retry_cached(ctx, &cached);
}

control_runtime_result_t control_home_server_complete(wl_ctx_t *ctx, control_runtime_t *runtime, uint32_t operation_id, home_response_t *response, control_encode_scratch_t scratch, wl_time_ms_t now_ms) {
  return control_home_server_finish(ctx, runtime, operation_id, 0, response, scratch, now_ms, false);
}

control_runtime_result_t control_home_server_reject(wl_ctx_t *ctx, control_runtime_t *runtime, uint32_t operation_id, int32_t application_status, home_response_t *response, control_encode_scratch_t scratch, wl_time_ms_t now_ms) {
  return control_home_server_finish(ctx, runtime, operation_id, application_status, response, scratch, now_ms, true);
}
