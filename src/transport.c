/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include "wirelink/wirelink.h"

#include "rx_ring.h"

int wl_set_sink(wl_ctx_t *ctx, wl_sink_fn sink, void *user_data) {
  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->in_callback) {
    return WL_ERR_REENTRANT;
  }
  ctx->sink = sink;
  ctx->sink_user_data = user_data;
  return WL_OK;
}

int wl_tx_cancel(wl_ctx_t *ctx, wl_tx_handle_t handle) {
  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (handle == 0U || handle != ctx->tx_handle) {
    return WL_ERR_NOT_FOUND;
  }
  if (ctx->tx_state == WL_TX_STATE_IDLE || ctx->tx_state == WL_TX_STATE_SUCCESS ||
      ctx->tx_state == WL_TX_STATE_FAILED || ctx->tx_state == WL_TX_STATE_CANCELLED) {
    return WL_ERR_INVALID_STATE;
  }
  if (ctx->tx_inflight != 0U) {
    ctx->tx_cancel_requested = 1U;
  } else {
    ctx->tx_queued = 0U;
  }
  ctx->tx_state = WL_TX_STATE_CANCELLED;
  ctx->tx_wait_state = WL_TX_WAIT_NONE;
  ctx->tx_waiting_seq = 0U;
  ctx->tx_retries_left = 0U;
  ctx->tx_result_code = WL_ERR_CANCELLED;
  return WL_OK;
}

int wl_tx_status(const wl_ctx_t *ctx, wl_tx_handle_t handle,
                 wl_tx_state_t *out_state) {
  if (ctx == NULL || out_state == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (handle == 0U || handle != ctx->tx_handle) {
    return WL_ERR_NOT_FOUND;
  }
  *out_state = ctx->tx_state;
  return WL_OK;
}

int wl_tx_take(wl_ctx_t *ctx, wl_tx_handle_t handle,
               wl_tx_result_t *out_result) {
  if (ctx == NULL || out_result == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (handle == 0U || handle != ctx->tx_handle) {
    return WL_ERR_NOT_FOUND;
  }
  if (ctx->tx_inflight != 0U ||
      (ctx->tx_state != WL_TX_STATE_SUCCESS &&
       ctx->tx_state != WL_TX_STATE_FAILED &&
       ctx->tx_state != WL_TX_STATE_CANCELLED)) {
    return WL_ERR_INVALID_STATE;
  }
  out_result->state = ctx->tx_state;
  out_result->result = ctx->tx_result_code;
  out_result->retries_used = ctx->tx_retries_used;
  ctx->tx_handle = 0U;
  ctx->tx_state = WL_TX_STATE_IDLE;
  ctx->tx_current_reliable = 0U;
  ctx->tx_cancel_requested = 0U;
  ctx->tx_wait_state = WL_TX_WAIT_NONE;
  ctx->tx_waiting_seq = 0U;
  ctx->tx_payload.length = 0U;
  return WL_OK;
}

int wl_feed_recover_reset(wl_ctx_t *ctx) {
  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->rx_event_leased != 0U) {
    return WL_ERR_WOULD_BLOCK;
  }
  (void)wl_rx_ring_consumer_consume(&ctx->rx_ring,
                                    wl_rx_ring_readable(&ctx->rx_ring));
  return WL_OK;
}

int wl_rx_get_counters(const wl_ctx_t *ctx, wl_rx_counters_t *out_counters) {
  if (ctx == NULL || out_counters == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  *out_counters = ctx->rx_counters;
  return WL_OK;
}

void wl_event_release(wl_ctx_t *ctx, const wl_event_t *event) {
  if (ctx == NULL || event == NULL) {
    return;
  }
  if ((event->type == WL_EVT_UNRELIABLE_RX ||
       event->type == WL_EVT_RELIABLE_RX) &&
      ctx->rx_event_leased != 0U &&
      event->lease == ctx->rx_event_generation) {
    ctx->rx_event_leased = 0U;
    if (ctx->rx_pending_consume != 0U) {
      (void)wl_rx_ring_consumer_consume(&ctx->rx_ring,
                                        ctx->rx_pending_consume);
      ctx->rx_pending_consume = 0U;
    }
    ctx->rx_candidate_source = 0U;
    ctx->rx_lease_source = 0U;
    ctx->rx_payload.length = 0U;
  }
}
