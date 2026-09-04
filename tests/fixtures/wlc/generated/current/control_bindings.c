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
    case ARM_MIT_COMMAND_MESSAGE_ID:
      if (router == NULL || router->arm_mit_command.handler == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_ROUTE;
        if (router != NULL) control_count(&router->counters.missing_route);
        break;
      }
      if (router->arm_mit_command.scratch == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_SCRATCH;
        control_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = arm_mit_command_decode(event->payload, event->payload_len, router->arm_mit_command.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_DISPATCH_CODEC_ERROR;
        control_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->arm_mit_command.handler(router->arm_mit_command.user_data, router->arm_mit_command.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = CONTROL_DISPATCH_HANDLER_ERROR;
        control_count(&router->counters.handler_failure);
        break;
      }
      result.domain = CONTROL_DISPATCH_OK;
      control_count(&router->counters.delivered);
      break;
    case HOME_REQUEST_MESSAGE_ID:
      if (router == NULL || router->home_request.handler == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_ROUTE;
        if (router != NULL) control_count(&router->counters.missing_route);
        break;
      }
      if (router->home_request.scratch == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_SCRATCH;
        control_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = home_request_decode(event->payload, event->payload_len, router->home_request.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_DISPATCH_CODEC_ERROR;
        control_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->home_request.handler(router->home_request.user_data, router->home_request.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = CONTROL_DISPATCH_HANDLER_ERROR;
        control_count(&router->counters.handler_failure);
        break;
      }
      result.domain = CONTROL_DISPATCH_OK;
      control_count(&router->counters.delivered);
      break;
    case HOME_RESPONSE_MESSAGE_ID:
      if (router == NULL || router->home_response.handler == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_ROUTE;
        if (router != NULL) control_count(&router->counters.missing_route);
        break;
      }
      if (router->home_response.scratch == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_SCRATCH;
        control_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = home_response_decode(event->payload, event->payload_len, router->home_response.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_DISPATCH_CODEC_ERROR;
        control_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->home_response.handler(router->home_response.user_data, router->home_response.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = CONTROL_DISPATCH_HANDLER_ERROR;
        control_count(&router->counters.handler_failure);
        break;
      }
      result.domain = CONTROL_DISPATCH_OK;
      control_count(&router->counters.delivered);
      break;
    case BULK_BEGIN_MESSAGE_ID:
      if (router == NULL || router->bulk_begin.handler == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_ROUTE;
        if (router != NULL) control_count(&router->counters.missing_route);
        break;
      }
      if (router->bulk_begin.scratch == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_SCRATCH;
        control_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = bulk_begin_decode(event->payload, event->payload_len, router->bulk_begin.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_DISPATCH_CODEC_ERROR;
        control_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->bulk_begin.handler(router->bulk_begin.user_data, router->bulk_begin.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = CONTROL_DISPATCH_HANDLER_ERROR;
        control_count(&router->counters.handler_failure);
        break;
      }
      result.domain = CONTROL_DISPATCH_OK;
      control_count(&router->counters.delivered);
      break;
    case BULK_CHUNK_MESSAGE_ID:
      if (router == NULL || router->bulk_chunk.handler == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_ROUTE;
        if (router != NULL) control_count(&router->counters.missing_route);
        break;
      }
      if (router->bulk_chunk.scratch == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_SCRATCH;
        control_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = bulk_chunk_decode(event->payload, event->payload_len, router->bulk_chunk.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_DISPATCH_CODEC_ERROR;
        control_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->bulk_chunk.handler(router->bulk_chunk.user_data, router->bulk_chunk.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = CONTROL_DISPATCH_HANDLER_ERROR;
        control_count(&router->counters.handler_failure);
        break;
      }
      result.domain = CONTROL_DISPATCH_OK;
      control_count(&router->counters.delivered);
      break;
    case BULK_END_MESSAGE_ID:
      if (router == NULL || router->bulk_end.handler == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_ROUTE;
        if (router != NULL) control_count(&router->counters.missing_route);
        break;
      }
      if (router->bulk_end.scratch == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_SCRATCH;
        control_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = bulk_end_decode(event->payload, event->payload_len, router->bulk_end.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_DISPATCH_CODEC_ERROR;
        control_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->bulk_end.handler(router->bulk_end.user_data, router->bulk_end.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = CONTROL_DISPATCH_HANDLER_ERROR;
        control_count(&router->counters.handler_failure);
        break;
      }
      result.domain = CONTROL_DISPATCH_OK;
      control_count(&router->counters.delivered);
      break;
    case BULK_ABORT_MESSAGE_ID:
      if (router == NULL || router->bulk_abort.handler == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_ROUTE;
        if (router != NULL) control_count(&router->counters.missing_route);
        break;
      }
      if (router->bulk_abort.scratch == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_SCRATCH;
        control_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = bulk_abort_decode(event->payload, event->payload_len, router->bulk_abort.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_DISPATCH_CODEC_ERROR;
        control_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->bulk_abort.handler(router->bulk_abort.user_data, router->bulk_abort.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = CONTROL_DISPATCH_HANDLER_ERROR;
        control_count(&router->counters.handler_failure);
        break;
      }
      result.domain = CONTROL_DISPATCH_OK;
      control_count(&router->counters.delivered);
      break;
    case BULK_STATUS_MESSAGE_ID:
      if (router == NULL || router->bulk_status.handler == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_ROUTE;
        if (router != NULL) control_count(&router->counters.missing_route);
        break;
      }
      if (router->bulk_status.scratch == NULL) {
        result.domain = CONTROL_DISPATCH_MISSING_SCRATCH;
        control_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = bulk_status_decode(event->payload, event->payload_len, router->bulk_status.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CONTROL_DISPATCH_CODEC_ERROR;
        control_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->bulk_status.handler(router->bulk_status.user_data, router->bulk_status.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
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

control_send_result_t control_joint_command_send(wl_ctx_t *ctx, const joint_command_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, JOINT_COMMAND_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = joint_command_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CONTROL_SEND_OK;
  return result;
}

control_send_result_t control_arm_command_send(wl_ctx_t *ctx, const arm_command_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, ARM_COMMAND_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = arm_command_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CONTROL_SEND_OK;
  return result;
}

control_send_result_t control_arm_mit_command_send(wl_ctx_t *ctx, const arm_mit_command_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, ARM_MIT_COMMAND_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = arm_mit_command_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CONTROL_SEND_OK;
  return result;
}

control_send_result_t control_home_request_send(wl_ctx_t *ctx, const home_request_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, HOME_REQUEST_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = home_request_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CONTROL_SEND_OK;
  return result;
}

control_send_result_t control_home_response_send(wl_ctx_t *ctx, const home_response_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, HOME_RESPONSE_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = home_response_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CONTROL_SEND_OK;
  return result;
}

control_send_result_t control_bulk_begin_send(wl_ctx_t *ctx, const bulk_begin_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, BULK_BEGIN_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = bulk_begin_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CONTROL_SEND_OK;
  return result;
}

control_send_result_t control_bulk_chunk_send(wl_ctx_t *ctx, const bulk_chunk_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, BULK_CHUNK_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = bulk_chunk_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CONTROL_SEND_OK;
  return result;
}

control_send_result_t control_bulk_end_send(wl_ctx_t *ctx, const bulk_end_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, BULK_END_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = bulk_end_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CONTROL_SEND_OK;
  return result;
}

control_send_result_t control_bulk_abort_send(wl_ctx_t *ctx, const bulk_abort_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, BULK_ABORT_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = bulk_abort_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CONTROL_SEND_OK;
  return result;
}

control_send_result_t control_bulk_status_send(wl_ctx_t *ctx, const bulk_status_t *message, wl_delivery_t delivery) {
  control_send_result_t result = { CONTROL_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, BULK_STATUS_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = bulk_status_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CONTROL_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CONTROL_SEND_OK;
  return result;
}
