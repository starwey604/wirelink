#include "control_bindings.h"

#include <limits.h>

static void control_count(uint32_t *counter) {
  if (*counter != UINT32_MAX) ++*counter;
}

control_dispatch_result_t control_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, control_router_t *router) {
  control_dispatch_result_t result = { CONTROL_DISPATCH_INVALID_ARGUMENT, 0U, WL_EVT_NONE, WL_CODEC_OK, 0 };
  if (event == NULL) return result;
  result.message_id = event->message_id;
  result.event_type = event->type;
  if (event->type != WL_EVT_UNRELIABLE_RX && event->type != WL_EVT_RELIABLE_RX) {
    result.domain = CONTROL_DISPATCH_NON_RX;
    if (router != NULL) control_count(&router->counters.non_rx);
    return result;
  }
  if (ctx == NULL) return result;
  switch (event->message_id) {
    case JOINT_COMMAND_MESSAGE_ID:
      if (router == NULL || router->joint_command.handler == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_ROUTE;
        if (router != NULL) control_count(&router->counters.missing_route);
        break;
      }
      if (router->joint_command.scratch == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_SCRATCH;
        control_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = joint_command_decode(event->payload, event->payload_len, router->joint_command.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_DISPATCH_CODEC_ERROR;
        control_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->joint_command.handler(router->joint_command.user_data, router->joint_command.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = CONTROL_DISPATCH_HANDLER_ERROR;
        control_count(&router->counters.handler_failure);
        break;
      }
      result.domain = CONTROL_DISPATCH_OK;
      control_count(&router->counters.delivered);
      break;
    case ARM_COMMAND_MESSAGE_ID:
      if (router == NULL || router->arm_command.handler == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_ROUTE;
        if (router != NULL) control_count(&router->counters.missing_route);
        break;
      }
      if (router->arm_command.scratch == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_SCRATCH;
        control_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = arm_command_decode(event->payload, event->payload_len, router->arm_command.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_DISPATCH_CODEC_ERROR;
        control_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->arm_command.handler(router->arm_command.user_data, router->arm_command.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = CONTROL_DISPATCH_HANDLER_ERROR;
        control_count(&router->counters.handler_failure);
        break;
      }
      result.domain = CONTROL_DISPATCH_OK;
      control_count(&router->counters.delivered);
      break;
    default:
      result.domain = CONTROL_DISPATCH_UNKNOWN_MESSAGE;
      if (router != NULL) control_count(&router->counters.unknown_message);
      break;
  }
  wl_event_release(ctx, event);
  return result;
}

control_send_result_t control_joint_command_send_unreliable(wl_ctx_t *ctx, const joint_command_t *message, control_encode_scratch_t scratch) {
  control_send_result_t result = { CONTROL_SEND_CODEC_ERROR, WL_CODEC_OK, WL_OK, WL_OK, 0U, 0U, 0U };
  result.codec_status = joint_command_encode(message, scratch.data, scratch.capacity, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) return result;
  result.core_called = 1U;
  result.core_result = wl_send_unreliable(ctx, JOINT_COMMAND_MESSAGE_ID, scratch.data, result.payload_length);
  result.domain = result.core_result == WL_OK ? CONTROL_SEND_OK : CONTROL_SEND_CORE_ERROR;
  return result;
}

control_send_result_t control_joint_command_send_reliable(wl_ctx_t *ctx, const joint_command_t *message, control_encode_scratch_t scratch) {
  control_send_result_t result = { CONTROL_SEND_CODEC_ERROR, WL_CODEC_OK, WL_OK, WL_OK, 0U, 0U, 0U };
  result.codec_status = joint_command_encode(message, scratch.data, scratch.capacity, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) return result;
  result.core_called = 1U;
  result.core_result = wl_send_reliable(ctx, JOINT_COMMAND_MESSAGE_ID, scratch.data, result.payload_length, &result.handle);
  result.domain = result.core_result == WL_OK ? CONTROL_SEND_OK : CONTROL_SEND_CORE_ERROR;
  return result;
}

control_send_result_t control_joint_command_send_direct(wl_ctx_t *ctx, const joint_command_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, WL_OK, 1U, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, JOINT_COMMAND_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = joint_command_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    result.abort_result = wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) {
    result.abort_result = wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.domain = CONTROL_SEND_OK;
  return result;
}

control_send_result_t control_arm_command_send_unreliable(wl_ctx_t *ctx, const arm_command_t *message, control_encode_scratch_t scratch) {
  control_send_result_t result = { CONTROL_SEND_CODEC_ERROR, WL_CODEC_OK, WL_OK, WL_OK, 0U, 0U, 0U };
  result.codec_status = arm_command_encode(message, scratch.data, scratch.capacity, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) return result;
  result.core_called = 1U;
  result.core_result = wl_send_unreliable(ctx, ARM_COMMAND_MESSAGE_ID, scratch.data, result.payload_length);
  result.domain = result.core_result == WL_OK ? CONTROL_SEND_OK : CONTROL_SEND_CORE_ERROR;
  return result;
}

control_send_result_t control_arm_command_send_reliable(wl_ctx_t *ctx, const arm_command_t *message, control_encode_scratch_t scratch) {
  control_send_result_t result = { CONTROL_SEND_CODEC_ERROR, WL_CODEC_OK, WL_OK, WL_OK, 0U, 0U, 0U };
  result.codec_status = arm_command_encode(message, scratch.data, scratch.capacity, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) return result;
  result.core_called = 1U;
  result.core_result = wl_send_reliable(ctx, ARM_COMMAND_MESSAGE_ID, scratch.data, result.payload_length, &result.handle);
  result.domain = result.core_result == WL_OK ? CONTROL_SEND_OK : CONTROL_SEND_CORE_ERROR;
  return result;
}

control_send_result_t control_arm_command_send_direct(wl_ctx_t *ctx, const arm_command_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, WL_OK, 1U, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, ARM_COMMAND_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = arm_command_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    result.abort_result = wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) {
    result.abort_result = wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.domain = CONTROL_SEND_OK;
  return result;
}
