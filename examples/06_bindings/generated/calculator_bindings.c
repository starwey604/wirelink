#include "calculator_bindings.h"

#include <limits.h>

static void calculator_count(uint32_t *counter) {
  if (*counter != UINT32_MAX) ++*counter;
}

calculator_dispatch_result_t calculator_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, calculator_router_t *router) {
  calculator_dispatch_result_t result = { CALCULATOR_DISPATCH_INVALID_ARGUMENT, 0U, WL_EVT_NONE, WL_CODEC_OK, 0 };
  if (event == NULL) return result;
  result.message_id = event->message_id;
  result.event_type = event->type;
  if (event->type != WL_EVT_UNRELIABLE_RX && event->type != WL_EVT_RELIABLE_RX) {
    result.domain = CALCULATOR_DISPATCH_NON_RX;
    if (router != NULL) calculator_count(&router->counters.non_rx);
    return result;
  }
  if (ctx == NULL) return result;
  switch (event->message_id) {
    case ADD_REQUEST_MESSAGE_ID:
      if (router == NULL || router->add_request.handler == NULL) {
        result.domain = CALCULATOR_DISPATCH_MISSING_ROUTE;
        if (router != NULL) calculator_count(&router->counters.missing_route);
        break;
      }
      if (router->add_request.scratch == NULL) {
        result.domain = CALCULATOR_DISPATCH_MISSING_SCRATCH;
        calculator_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = add_request_decode(event->payload, event->payload_len, router->add_request.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CALCULATOR_DISPATCH_CODEC_ERROR;
        calculator_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->add_request.handler(router->add_request.user_data, router->add_request.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = CALCULATOR_DISPATCH_HANDLER_ERROR;
        calculator_count(&router->counters.handler_failure);
        break;
      }
      result.domain = CALCULATOR_DISPATCH_OK;
      calculator_count(&router->counters.delivered);
      break;
    case ADD_RESPONSE_MESSAGE_ID:
      if (router == NULL || router->add_response.handler == NULL) {
        result.domain = CALCULATOR_DISPATCH_MISSING_ROUTE;
        if (router != NULL) calculator_count(&router->counters.missing_route);
        break;
      }
      if (router->add_response.scratch == NULL) {
        result.domain = CALCULATOR_DISPATCH_MISSING_SCRATCH;
        calculator_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = add_response_decode(event->payload, event->payload_len, router->add_response.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = CALCULATOR_DISPATCH_CODEC_ERROR;
        calculator_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->add_response.handler(router->add_response.user_data, router->add_response.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = CALCULATOR_DISPATCH_HANDLER_ERROR;
        calculator_count(&router->counters.handler_failure);
        break;
      }
      result.domain = CALCULATOR_DISPATCH_OK;
      calculator_count(&router->counters.delivered);
      break;
    default:
      result.domain = CALCULATOR_DISPATCH_UNKNOWN_MESSAGE;
      if (router != NULL) calculator_count(&router->counters.unknown_message);
      break;
  }
  wl_event_release(ctx, event);
  return result;
}

calculator_send_result_t calculator_add_request_send(wl_ctx_t *ctx, const add_request_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms) {
  calculator_send_result_t result = { CALCULATOR_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, ADD_REQUEST_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = add_request_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CALCULATOR_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, now_ms, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CALCULATOR_SEND_OK;
  return result;
}

calculator_send_result_t calculator_add_response_send(wl_ctx_t *ctx, const add_response_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms) {
  calculator_send_result_t result = { CALCULATOR_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, ADD_RESPONSE_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = add_response_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = CALCULATOR_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, now_ms, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = CALCULATOR_SEND_OK;
  return result;
}
