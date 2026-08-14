/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include "wirelink/wirelink.h"

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
  if (ctx->tx_state == WL_TX_STATE_IDLE || ctx->tx_state == WL_TX_STATE_SUCCESS) {
    return WL_ERR_INVALID_STATE;
  }
  ctx->tx_state = WL_TX_STATE_CANCELLED;
  ctx->tx_waiting_ack = 0U;
  ctx->tx_waiting_seq = 0U;
  ctx->tx_inflight = 0;
  ctx->in_flight_reliable = 0;
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

int wl_feed_recover_reset(wl_ctx_t *ctx) {
  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  ctx->cobs_accum_len = 0;
  ctx->cobs_overflow = 0;
  return wl_bb_reset(&ctx->rx_fifo);
}

void wl_event_release(wl_ctx_t *ctx, const wl_event_t *event) {
  (void)ctx;
  (void)event;
}
