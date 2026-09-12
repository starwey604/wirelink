#include "device_bindings.h"

#include <limits.h>

static void device_count(uint32_t *counter) {
  if (*counter != UINT32_MAX) ++*counter;
}

device_dispatch_result_t device_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, device_router_t *router) {
  device_dispatch_result_t result = { DEVICE_DISPATCH_INVALID_ARGUMENT, 0U, WL_EVT_NONE, WL_CODEC_OK, 0 };
  if (event == NULL) return result;
  result.message_id = event->message_id;
  result.event_type = event->type;
  if (event->type != WL_EVT_UNRELIABLE_RX && event->type != WL_EVT_RELIABLE_RX) {
    result.domain = DEVICE_DISPATCH_NON_RX;
    if (router != NULL) device_count(&router->counters.non_rx);
    return result;
  }
  if (ctx == NULL) return result;
  switch (event->message_id) {
    case INFO_REQUEST_MESSAGE_ID:
      if (router == NULL || router->info_request.handler == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_ROUTE;
        if (router != NULL) device_count(&router->counters.missing_route);
        break;
      }
      if (router->info_request.scratch == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_SCRATCH;
        device_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = info_request_decode(event->payload, event->payload_len, router->info_request.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_DISPATCH_CODEC_ERROR;
        device_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->info_request.handler(router->info_request.user_data, router->info_request.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = DEVICE_DISPATCH_HANDLER_ERROR;
        device_count(&router->counters.handler_failure);
        break;
      }
      result.domain = DEVICE_DISPATCH_OK;
      device_count(&router->counters.delivered);
      break;
    case SETTINGS_MESSAGE_ID:
      if (router == NULL || router->settings.handler == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_ROUTE;
        if (router != NULL) device_count(&router->counters.missing_route);
        break;
      }
      if (router->settings.scratch == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_SCRATCH;
        device_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = settings_decode(event->payload, event->payload_len, router->settings.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_DISPATCH_CODEC_ERROR;
        device_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->settings.handler(router->settings.user_data, router->settings.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = DEVICE_DISPATCH_HANDLER_ERROR;
        device_count(&router->counters.handler_failure);
        break;
      }
      result.domain = DEVICE_DISPATCH_OK;
      device_count(&router->counters.delivered);
      break;
    case INFO_RESPONSE_MESSAGE_ID:
      if (router == NULL || router->info_response.handler == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_ROUTE;
        if (router != NULL) device_count(&router->counters.missing_route);
        break;
      }
      if (router->info_response.scratch == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_SCRATCH;
        device_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = info_response_decode(event->payload, event->payload_len, router->info_response.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_DISPATCH_CODEC_ERROR;
        device_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->info_response.handler(router->info_response.user_data, router->info_response.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = DEVICE_DISPATCH_HANDLER_ERROR;
        device_count(&router->counters.handler_failure);
        break;
      }
      result.domain = DEVICE_DISPATCH_OK;
      device_count(&router->counters.delivered);
      break;
    case CONFIGURE_REQUEST_MESSAGE_ID:
      if (router == NULL || router->configure_request.handler == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_ROUTE;
        if (router != NULL) device_count(&router->counters.missing_route);
        break;
      }
      if (router->configure_request.scratch == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_SCRATCH;
        device_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = configure_request_decode(event->payload, event->payload_len, router->configure_request.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_DISPATCH_CODEC_ERROR;
        device_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->configure_request.handler(router->configure_request.user_data, router->configure_request.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = DEVICE_DISPATCH_HANDLER_ERROR;
        device_count(&router->counters.handler_failure);
        break;
      }
      result.domain = DEVICE_DISPATCH_OK;
      device_count(&router->counters.delivered);
      break;
    case CONFIGURE_RESPONSE_MESSAGE_ID:
      if (router == NULL || router->configure_response.handler == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_ROUTE;
        if (router != NULL) device_count(&router->counters.missing_route);
        break;
      }
      if (router->configure_response.scratch == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_SCRATCH;
        device_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = configure_response_decode(event->payload, event->payload_len, router->configure_response.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_DISPATCH_CODEC_ERROR;
        device_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->configure_response.handler(router->configure_response.user_data, router->configure_response.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = DEVICE_DISPATCH_HANDLER_ERROR;
        device_count(&router->counters.handler_failure);
        break;
      }
      result.domain = DEVICE_DISPATCH_OK;
      device_count(&router->counters.delivered);
      break;
    case NUMBERS_MESSAGE_ID:
      if (router == NULL || router->numbers.handler == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_ROUTE;
        if (router != NULL) device_count(&router->counters.missing_route);
        break;
      }
      if (router->numbers.scratch == NULL) {
        result.domain = DEVICE_DISPATCH_MISSING_SCRATCH;
        device_count(&router->counters.missing_scratch);
        break;
      }
      result.codec_status = numbers_decode(event->payload, event->payload_len, router->numbers.scratch);
      if (result.codec_status != WL_CODEC_OK) {
        result.domain = DEVICE_DISPATCH_CODEC_ERROR;
        device_count(&router->counters.codec_failure);
        break;
      }
      result.handler_result = router->numbers.handler(router->numbers.user_data, router->numbers.scratch, event->type == WL_EVT_RELIABLE_RX ? WL_DELIVERY_RELIABLE : WL_DELIVERY_UNRELIABLE);
      if (result.handler_result != 0) {
        result.domain = DEVICE_DISPATCH_HANDLER_ERROR;
        device_count(&router->counters.handler_failure);
        break;
      }
      result.domain = DEVICE_DISPATCH_OK;
      device_count(&router->counters.delivered);
      break;
    default:
      result.domain = DEVICE_DISPATCH_UNKNOWN_MESSAGE;
      if (router != NULL) device_count(&router->counters.unknown_message);
      break;
  }
  wl_event_release(ctx, event);
  return result;
}

device_send_result_t device_info_request_send(wl_ctx_t *ctx, const info_request_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms) {
  device_send_result_t result = { DEVICE_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, INFO_REQUEST_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = info_request_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = DEVICE_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, now_ms, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = DEVICE_SEND_OK;
  return result;
}

device_send_result_t device_settings_send(wl_ctx_t *ctx, const settings_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms) {
  device_send_result_t result = { DEVICE_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, SETTINGS_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = settings_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = DEVICE_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, now_ms, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = DEVICE_SEND_OK;
  return result;
}

device_send_result_t device_info_response_send(wl_ctx_t *ctx, const info_response_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms) {
  device_send_result_t result = { DEVICE_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, INFO_RESPONSE_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = info_response_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = DEVICE_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, now_ms, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = DEVICE_SEND_OK;
  return result;
}

device_send_result_t device_configure_request_send(wl_ctx_t *ctx, const configure_request_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms) {
  device_send_result_t result = { DEVICE_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, CONFIGURE_REQUEST_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = configure_request_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = DEVICE_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, now_ms, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = DEVICE_SEND_OK;
  return result;
}

device_send_result_t device_configure_response_send(wl_ctx_t *ctx, const configure_response_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms) {
  device_send_result_t result = { DEVICE_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, CONFIGURE_RESPONSE_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = configure_response_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = DEVICE_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, now_ms, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = DEVICE_SEND_OK;
  return result;
}

device_send_result_t device_numbers_send(wl_ctx_t *ctx, const numbers_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms) {
  device_send_result_t result = { DEVICE_SEND_CORE_ERROR, WL_CODEC_OK, WL_OK, 0U, 0U };
  wl_tx_payload_claim_t claim = {0};
  result.core_result = wl_tx_payload_claim(ctx, NUMBERS_MESSAGE_ID, delivery, &claim);
  if (result.core_result != WL_OK) return result;
  result.codec_status = numbers_encode(message, claim.span.data, claim.span.length, &result.payload_length);
  if (result.codec_status != WL_CODEC_OK) {
    result.domain = DEVICE_SEND_CODEC_ERROR;
    (void)wl_tx_payload_abort(ctx, &claim);
    return result;
  }
  result.core_result = wl_tx_payload_commit(ctx, &claim, result.payload_length, now_ms, delivery == WL_DELIVERY_RELIABLE ? &result.handle : NULL);
  if (result.core_result != WL_OK) return result;
  result.domain = DEVICE_SEND_OK;
  return result;
}
