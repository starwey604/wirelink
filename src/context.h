/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_SRC_CONTEXT_H_
#define WIRELINK_SRC_CONTEXT_H_

#include <stddef.h>
#include <stdint.h>

#include "wirelink/rx_ring_state.h"
#include "wirelink/wirelink.h"

typedef struct {
  wl_rx_ring_state_t rx_ring;

  wl_config_t config;
  wl_storage_t storage;
  uint8_t initialized;

  wl_sink_fn sink;
  void *sink_user_data;

  wl_tx_state_t tx_state;
  wl_tx_handle_t tx_handle;
  wl_tx_handle_t tx_next_handle;
  wl_io_token_t tx_token;
  wl_time_ms_t tx_start_ts;
  uint16_t tx_retries_left;
  uint16_t tx_retries_max;
  uint16_t tx_retries_used;
  uint32_t tx_sequence;
  uint32_t tx_retry_sequence;
  uint32_t tx_waiting_seq;
  uint8_t tx_inflight;
  wl_tx_wait_reason_t tx_wait_state;
  uint8_t tx_current_reliable;
  uint8_t tx_cancel_requested;
  uint16_t tx_generation;
  int tx_result_code;
  uint16_t tx_last_message_id;
  uint8_t tx_last_flags;

  uint64_t session_id;
  uint32_t rx_sequence;
  uint64_t rx_session_id;
  uint8_t rx_have_reliable;

  wl_event_t event;
  uint8_t has_event;
  uint8_t in_callback;
  uint8_t in_flight_reliable;
  uint8_t control_pending;
  uint8_t control_inflight;
  uint8_t tx_queued;
  size_t control_len;
  uint8_t rx_event_leased;
  uint8_t rx_candidate_source;
  uint32_t rx_event_generation;
  size_t rx_pending_consume;
  wl_rx_counters_t rx_counters;

  wl_time_ms_t now_ms;

  wl_span_t rx_payload;
  wl_span_t tx_payload;
} wl_ctx_impl_t;

_Static_assert(sizeof(wl_ctx_impl_t) <= WL_CONTEXT_STORAGE_SIZE,
               "WL_CONTEXT_STORAGE_SIZE is too small for wl_ctx_impl_t");
_Static_assert(_Alignof(wl_ctx_impl_t) <= _Alignof(wl_ctx_t),
               "wl_ctx_t does not satisfy implementation alignment");

static inline wl_ctx_impl_t *wl_ctx_impl(wl_ctx_t *ctx) {
  return (wl_ctx_impl_t *)(void *)ctx;
}

static inline const wl_ctx_impl_t *wl_ctx_impl_const(const wl_ctx_t *ctx) {
  return (const wl_ctx_impl_t *)(const void *)ctx;
}

#endif /* WIRELINK_SRC_CONTEXT_H_ */
