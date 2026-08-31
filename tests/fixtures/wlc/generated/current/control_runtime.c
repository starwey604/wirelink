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

control_runtime_result_t control_runtime_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, control_runtime_t *runtime) {
  control_runtime_result_t result = control_runtime_result(event);
  if (event == NULL) return result;
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
    default:
      result.domain = CONTROL_RUNTIME_UNKNOWN_MESSAGE;
      break;
  }

release_event:
  wl_event_release(ctx, event);
  return result;
}
