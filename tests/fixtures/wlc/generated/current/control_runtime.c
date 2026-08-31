#include "control_runtime.h"

static control_runtime_result_t control_runtime_result(const wl_event_t *event) {
  control_runtime_result_t result = {0};
  result.domain = CONTROL_RUNTIME_INVALID_ARGUMENT;
  result.codec_status = WL_CODEC_OK;
  result.storage_result = WL_OK;
  result.abort_result = WL_OK;
  result.rpc_result = WL_RPC_OK;
  result.core_result = WL_OK;
  result.rpc_disposition = WL_RPC_SERVER_NEW;
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

control_runtime_result_t control_runtime_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, control_runtime_t *runtime, wl_time_ms_t now_ms) {
  control_runtime_result_t result = control_runtime_result(event);
  if (event == NULL) return result;
  (void)now_ms;
  if (event->type == WL_EVT_TX_SUCCESS || event->type == WL_EVT_TX_TIMEOUT || event->type == WL_EVT_TX_FAILED) {
    if (runtime == NULL || runtime->rpc_client == NULL) {
      result.domain = CONTROL_RUNTIME_NON_RX;
      return result;
    }
    result.rpc_result = wl_rpc_client_on_tx_event(runtime->rpc_client, event);
    if (result.rpc_result == WL_RPC_OK) result.domain = CONTROL_RUNTIME_OK;
    else if (result.rpc_result == WL_RPC_ERR_NOT_FOUND) result.domain = CONTROL_RUNTIME_NON_RX;
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
      if (event->type != WL_EVT_RELIABLE_RX) {
        result.domain = CONTROL_RUNTIME_DELIVERY_MISMATCH;
        break;
      }
      if (runtime->joint_command_fifo == NULL) {
        result.domain = CONTROL_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.storage_result = wl_fifo_write_claim(runtime->joint_command_fifo, &claim);
      if (result.storage_result != WL_OK) {
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      if (claim.value_size < sizeof(joint_command_t)) {
        result.storage_result = WL_ERR_BUF_TOO_SMALL;
        result.abort_result = wl_fifo_write_abort(runtime->joint_command_fifo, &claim);
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      if (((uintptr_t)claim.value % _Alignof(joint_command_t)) != 0U) {
        result.storage_result = WL_ERR_INVALID_ARG;
        result.abort_result = wl_fifo_write_abort(runtime->joint_command_fifo, &claim);
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      result.codec_status = joint_command_decode(event->payload, event->payload_len, (joint_command_t *)claim.value);
      if (result.codec_status != WL_CODEC_OK) {
        result.abort_result = wl_fifo_write_abort(runtime->joint_command_fifo, &claim);
        result.domain = CONTROL_RUNTIME_CODEC_ERROR;
        break;
      }
      result.storage_result = wl_fifo_write_publish(runtime->joint_command_fifo, &claim);
      if (result.storage_result != WL_OK) {
        result.abort_result = wl_fifo_write_abort(runtime->joint_command_fifo, &claim);
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      result.domain = CONTROL_RUNTIME_OK;
      break;
    }
    case ARM_MIT_COMMAND_MESSAGE_ID: {
      wl_latest_write_claim_t claim = {0};
      if (event->type != WL_EVT_UNRELIABLE_RX) {
        result.domain = CONTROL_RUNTIME_DELIVERY_MISMATCH;
        break;
      }
      if (runtime->arm_mit_command_latest == NULL) {
        result.domain = CONTROL_RUNTIME_MISSING_ROUTE;
        break;
      }
      result.storage_result = wl_latest_write_claim(runtime->arm_mit_command_latest, &claim);
      if (result.storage_result != WL_OK) {
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      if (claim.value_size < sizeof(arm_mit_command_t)) {
        result.storage_result = WL_ERR_BUF_TOO_SMALL;
        result.abort_result = wl_latest_write_abort(runtime->arm_mit_command_latest, &claim);
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      if (((uintptr_t)claim.value % _Alignof(arm_mit_command_t)) != 0U) {
        result.storage_result = WL_ERR_INVALID_ARG;
        result.abort_result = wl_latest_write_abort(runtime->arm_mit_command_latest, &claim);
        result.domain = CONTROL_RUNTIME_STORAGE_ERROR;
        break;
      }
      result.codec_status = arm_mit_command_decode(event->payload, event->payload_len, (arm_mit_command_t *)claim.value);
      if (result.codec_status != WL_CODEC_OK) {
        result.abort_result = wl_latest_write_abort(runtime->arm_mit_command_latest, &claim);
        result.domain = CONTROL_RUNTIME_CODEC_ERROR;
        break;
      }
      result.storage_result = wl_latest_write_publish(runtime->arm_mit_command_latest, &claim);
      if (result.storage_result != WL_OK) {
        result.abort_result = wl_latest_write_abort(runtime->arm_mit_command_latest, &claim);
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
      result.codec_status = home_request_decode(event->payload, event->payload_len, runtime->home.request_scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_RUNTIME_CODEC_ERROR;
        break;
      }
      if (!runtime->home.request_scratch->has_operation_id || runtime->home.request_scratch->operation_id == 0U) {
        result.rpc_result = WL_RPC_ERR_INVALID_ARG;
        result.domain = CONTROL_RUNTIME_RPC_ERROR;
        break;
      }
      result.operation_id = runtime->home.request_scratch->operation_id;
      result.codec_status = home_request_encode(runtime->home.request_scratch, runtime->home.canonical_request_scratch.data, runtime->home.canonical_request_scratch.capacity, &canonical_length);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_RUNTIME_CODEC_ERROR;
        break;
      }
      result.payload_length = canonical_length;
      identity.operation_id = result.operation_id;
      identity.request_message_id = HOME_REQUEST_MESSAGE_ID;
      identity.response_message_id = HOME_RESPONSE_MESSAGE_ID;
      identity.request_fingerprint = control_rpc_request_fingerprint(runtime->home.canonical_request_scratch.data, canonical_length);
      result.rpc_result = wl_rpc_server_begin(runtime->rpc_server, &identity, now_ms, &result.rpc_disposition, &replay);
      if (result.rpc_result != WL_RPC_OK) {
        result.domain = CONTROL_RUNTIME_RPC_ERROR;
        break;
      }
      result.server_response = replay;
      switch (result.rpc_disposition) {
        case WL_RPC_SERVER_NEW:
          if (runtime->home.request_handler == NULL) {
            result.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, result.operation_id);
            result.domain = CONTROL_RUNTIME_MISSING_ROUTE;
            break;
          }
          result.application_result = runtime->home.request_handler(runtime->home.user_data, runtime->home.request_scratch, WL_DELIVERY_RELIABLE);
          if (result.application_result != 0) {
            result.rpc_result = wl_rpc_server_abandon(runtime->rpc_server, result.operation_id);
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
          result.core_result = retry.core_result;
          result.handle = retry.handle;
          result.payload_length = retry.payload_length;
          result.application_result = retry.application_result;
          result.domain = retry.domain;
          break;
        case WL_RPC_SERVER_CONFLICT:
          result.rpc_result = WL_RPC_ERR_OPERATION_CONFLICT;
          result.domain = CONTROL_RUNTIME_RPC_ERROR;
          break;
        default:
          result.rpc_result = WL_RPC_ERR_INVALID_STATE;
          result.domain = CONTROL_RUNTIME_RPC_ERROR;
          break;
      }
      break;
    }
    case HOME_RESPONSE_MESSAGE_ID: {
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
      result.codec_status = home_response_decode(event->payload, event->payload_len, runtime->home.response_scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_RUNTIME_CODEC_ERROR;
        break;
      }
      if (!runtime->home.response_scratch->has_operation_id || runtime->home.response_scratch->operation_id == 0U || !runtime->home.response_scratch->has_status) {
        result.rpc_result = WL_RPC_ERR_RESPONSE_MISMATCH;
        result.domain = CONTROL_RUNTIME_RPC_ERROR;
        break;
      }
      result.operation_id = runtime->home.response_scratch->operation_id;
      result.application_result = (int32_t)runtime->home.response_scratch->status;
      result.payload_length = event->payload_len;
      result.rpc_result = wl_rpc_client_on_response(runtime->rpc_client, HOME_RESPONSE_MESSAGE_ID, result.operation_id, result.application_result, event->payload, event->payload_len);
      result.domain = result.rpc_result == WL_RPC_OK ? CONTROL_RUNTIME_OK : CONTROL_RUNTIME_RPC_ERROR;
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
  result.operation_id = operation_id;
  result.codec_status = sent.codec_status;
  result.core_result = sent.core_result;
  result.abort_result = sent.abort_result;
  result.handle = sent.handle;
  result.payload_length = sent.payload_length;
  if (sent.domain == CONTROL_SEND_CODEC_ERROR) {
    result.rpc_result = wl_rpc_client_link_failed(runtime->rpc_client, operation_id, WL_ERR_CORRUPT_PAYLOAD);
    result.domain = CONTROL_RUNTIME_CODEC_ERROR;
    return result;
  }
  if (sent.domain == CONTROL_SEND_CORE_ERROR) {
    result.rpc_result = wl_rpc_client_link_failed(runtime->rpc_client, operation_id, sent.core_result);
    result.domain = CONTROL_RUNTIME_CORE_ERROR;
    return result;
  }
  result.rpc_result = wl_rpc_client_bind_tx(runtime->rpc_client, operation_id, result.handle);
  result.domain = result.rpc_result == WL_RPC_OK ? CONTROL_RUNTIME_OK : CONTROL_RUNTIME_RPC_ERROR;
  return result;
}

control_runtime_result_t control_home_client_start_scratch(wl_ctx_t *ctx, control_runtime_t *runtime, home_request_t *request, uint32_t timeout_ms, wl_time_ms_t now_ms, control_encode_scratch_t scratch) {
  control_runtime_result_t result = control_runtime_result(NULL);
  control_send_result_t sent;
  uint32_t operation_id = 0U;
  result.message_id = HOME_REQUEST_MESSAGE_ID;
  if (ctx == NULL || runtime == NULL || runtime->rpc_client == NULL || request == NULL) return result;
  result.rpc_result = wl_rpc_client_begin(runtime->rpc_client, HOME_REQUEST_MESSAGE_ID, HOME_RESPONSE_MESSAGE_ID, timeout_ms, now_ms, &operation_id);
  result.operation_id = operation_id;
  if (result.rpc_result != WL_RPC_OK) {
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
  if (ctx == NULL || runtime == NULL || runtime->rpc_client == NULL || request == NULL) return result;
  result.rpc_result = wl_rpc_client_begin(runtime->rpc_client, HOME_REQUEST_MESSAGE_ID, HOME_RESPONSE_MESSAGE_ID, timeout_ms, now_ms, &operation_id);
  result.operation_id = operation_id;
  if (result.rpc_result != WL_RPC_OK) {
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
  if (ctx == NULL || cached == NULL || cached->identity.operation_id == 0U || cached->identity.request_message_id != HOME_REQUEST_MESSAGE_ID || cached->identity.response_message_id != HOME_RESPONSE_MESSAGE_ID || (cached->response_length != 0U && cached->response_data == NULL)) return result;
  result.operation_id = cached->identity.operation_id;
  result.application_result = cached->application_status;
  result.payload_length = cached->response_length;
  result.server_response = *cached;
  result.core_result = wl_send_reliable(ctx, HOME_RESPONSE_MESSAGE_ID, cached->response_data, cached->response_length, &result.handle);
  result.domain = result.core_result == WL_OK ? CONTROL_RUNTIME_OK : CONTROL_RUNTIME_CORE_ERROR;
  return result;
}

static control_runtime_result_t control_home_server_finish(wl_ctx_t *ctx, control_runtime_t *runtime, uint32_t operation_id, int32_t application_status, home_response_t *response, control_encode_scratch_t scratch, wl_time_ms_t now_ms, bool reject) {
  control_runtime_result_t result = control_runtime_result(NULL);
  wl_rpc_server_response_t cached = {0};
  size_t encoded_length = 0U;
  result.message_id = HOME_RESPONSE_MESSAGE_ID;
  result.operation_id = operation_id;
  result.application_result = application_status;
  if (ctx == NULL || runtime == NULL || runtime->rpc_server == NULL || operation_id == 0U || response == NULL) return result;
  if (reject && application_status == 0) {
    result.rpc_result = WL_RPC_ERR_INVALID_ARG;
    result.domain = CONTROL_RUNTIME_RPC_ERROR;
    return result;
  }
  response->has_operation_id = true;
  response->operation_id = operation_id;
  response->has_status = true;
  response->status = application_status;
  result.codec_status = home_response_encode(response, scratch.data, scratch.capacity, &encoded_length);
  result.payload_length = encoded_length;
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_RUNTIME_CODEC_ERROR;
    return result;
  }
  if (reject) result.rpc_result = wl_rpc_server_reject(runtime->rpc_server, operation_id, application_status, scratch.data, encoded_length, now_ms, &cached);
  else result.rpc_result = wl_rpc_server_complete(runtime->rpc_server, operation_id, application_status, scratch.data, encoded_length, now_ms, &cached);
  if (result.rpc_result != WL_RPC_OK) {
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
