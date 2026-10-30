/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include "wirelink/wirelink.h"

#include "context.h"
#include "rx_ring.h"
#include "unit_rx.h"

static int wl_rx_runnable(const wl_ctx_t *ctx, const wl_ctx_impl_t *impl) {
  size_t delimiter_offset;

  if (impl->rx_event_leased != 0U) {
    return 0;
  }
  if (impl->config.envelope == WL_ENVELOPE_COBS_STREAM &&
      (wl_rx_ring_consumer_overflow_pending(&impl->rx_ring) != 0 ||
       wl_rx_ring_consumer_find(&impl->rx_ring, 0U, &delimiter_offset) ==
           WL_OK)) {
    return 1;
  }
  return wl_rx_unit_consumer_has_data(ctx);
}

wl_err_t wl_link_get_readiness(const wl_ctx_t *ctx, wl_time_ms_t now_ms,
                     wl_readiness_t *out_hint) {
  const wl_ctx_impl_t *impl;

  if (out_hint == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  *out_hint = (wl_readiness_t){
      .runnable = 0U,
      .next_deadline_ms = WL_NO_DEADLINE,
  };
  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  impl = wl_ctx_impl_const(ctx);
  if (impl->initialized == 0U) {
    return WL_ERR_NOT_INITIALIZED;
  }

  if (impl->has_event != 0U || impl->tx_unreliable_completions != 0U ||
      (impl->tx_pending_event != WL_EVT_NONE &&
       !(impl->tx_inflight != 0U && impl->in_flight_reliable != 0U)) ||
      (impl->control_pending == 0U && wl_rx_runnable(ctx, impl) != 0)) {
    out_hint->runnable = 1U;
  }

  if (impl->control_pending || impl->tx_queued) {
    if (impl->tx_writable && !impl->tx_inflight && !impl->control_inflight &&
        !impl->tx_drain_pending && !impl->tx_claim_active)
      out_hint->runnable = 1U;
    else out_hint->blocked = 1U;
  }
  out_hint->blocked |= impl->tx_inflight || impl->control_inflight ||
      impl->tx_drain_pending || impl->rx_event_leased || impl->tx_claim_active;

  /*
   * A pending control unit has priority over DATA and suppresses timeout work
   * in wl_poll().  Its sink already returned BUSY or another unit is still in
   * flight, so an adapter activity notification—not a zero-delay loop—must
   * wake the consumer. DATA may now be unreliable while a reliable ACK timer
   * expires; that timer cannot reuse the physical unit until I/O completes.
   * An application holding a claim must commit/abort before retry work resumes.
   */
  if (impl->control_pending == 0U &&
      impl->tx_inflight == 0U && impl->tx_queued == 0U &&
      impl->tx_claim_active == 0U &&
      impl->tx_state == WL_TX_STATE_WAITING_ACK &&
      impl->tx_wait_state == WL_TX_WAIT_ACK &&
      impl->config.ack_timeout_ms != 0U) {
    const wl_time_ms_t elapsed = now_ms - impl->tx_start_ts;

    if (elapsed >= impl->config.ack_timeout_ms) {
      out_hint->next_deadline_ms = 0U;
      out_hint->runnable = 1U;
    } else {
      out_hint->next_deadline_ms = impl->config.ack_timeout_ms - elapsed;
      out_hint->blocked = 1U;
    }
  }
  return WL_OK;
}
