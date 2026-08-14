/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include "wirelink/wirelink.h"

int wl_init(wl_ctx_t *ctx, const wl_config_t *config, uint8_t *rx_mem,
            size_t rx_mem_size, uint8_t *tx_mem, size_t tx_mem_size) {
  if (ctx == NULL || config == NULL || rx_mem == NULL || tx_mem == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (config->max_payload_len == 0U ||
      config->max_payload_len > WL_FRAME_MAX_PAYLOAD) {
    return WL_ERR_INVALID_ARG;
  }
  if (config->session_id == 0ULL) {
    return WL_ERR_INVALID_ARG;
  }

  if (wl_bb_init(&ctx->rx_fifo, rx_mem, rx_mem_size) != WL_OK) {
    return WL_ERR_INVALID_STATE;
  }
  if (wl_bb_init(&ctx->tx_fifo, tx_mem, tx_mem_size) != WL_OK) {
    return WL_ERR_INVALID_STATE;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->config = config;
  ctx->tx_retries_max =
      (config->max_retries != 0U) ? config->max_retries : 0U;
  ctx->tx_retries_left = ctx->tx_retries_max;
  ctx->session_id = config->session_id;
  ctx->tx_handle = 0U;
  ctx->tx_token = 1U;
  ctx->tx_next_handle = 1U;
  ctx->tx_state = WL_TX_STATE_IDLE;
  ctx->tx_waiting_ack = 0U;
  ctx->tx_waiting_seq = 0U;
  ctx->cobs_accum_len = 0;
  ctx->cobs_overflow = 0;

  return WL_OK;
}
