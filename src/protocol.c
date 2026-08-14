/* SPDX-License-Identifier: Apache-2.0 */

#include <limits.h>
#include <string.h>

#include "wirelink/cobs.h"
#include "wirelink/wirelink.h"

#include "rx_ring.h"

enum {
  WL_RX_SOURCE_NONE = 0,
  WL_RX_SOURCE_RING,
  WL_RX_SOURCE_FALLBACK,
};

static int wl_feed_unit_raw(wl_ctx_t *ctx, const uint8_t *unit, size_t len);
static int wl_send_ack(wl_ctx_t *ctx, const wl_frame_view_t *view);
static int wl_handle_ack(wl_ctx_t *ctx, const wl_frame_view_t *view);
static int wl_send_tx_payload(wl_ctx_t *ctx, uint8_t retrying);
static int wl_submit_control(wl_ctx_t *ctx);
static void wl_process_rx_stream(wl_ctx_t *ctx);
static void wl_prepare_tx_payload(wl_ctx_t *ctx, const wl_wire_packet_t *pkt,
                                 uint8_t reliable);

static int wl_push_event(wl_ctx_t *ctx, wl_event_type_t type, uint16_t cmd_id,
                        const uint8_t *payload, size_t payload_len,
                        wl_tx_handle_t handle) {
  const uint8_t is_rx = (type == WL_EVT_UNRELIABLE_RX ||
                         type == WL_EVT_RELIABLE_RX);
  if (ctx->has_event || (is_rx != 0U && ctx->rx_event_leased != 0U)) {
    return WL_ERR_QUEUE_FULL;
  }
  if (is_rx != 0U) {
    if (payload_len > ctx->config->max_payload_len ||
        (payload_len != 0U && payload == NULL) ||
        ctx->rx_candidate_source == WL_RX_SOURCE_NONE) {
      return WL_ERR_PAYLOAD_TOO_LONG;
    }
    ctx->rx_payload.data = (uint8_t *)(uintptr_t)payload;
    ctx->rx_payload.length = payload_len;
    ctx->rx_lease_source = ctx->rx_candidate_source;
    ctx->rx_event_generation++;
    if (ctx->rx_event_generation == 0U) {
      ctx->rx_event_generation = 1U;
    }
  }

  ctx->event.type = type;
  ctx->event.cmd_id = cmd_id;
  ctx->event.payload = (is_rx != 0U) ? ctx->rx_payload.data : payload;
  ctx->event.payload_len = payload_len;
  ctx->event.handle = handle;
  ctx->event.io_result = 0;
  ctx->event.lease = (is_rx != 0U) ? ctx->rx_event_generation : 0U;
  ctx->has_event = 1U;
  return WL_OK;
}

static int wl_send_tx_payload(wl_ctx_t *ctx, uint8_t retrying) {
  wl_wire_packet_t wire = {
      .type = WL_PACKET_DATA,
      .integrity = ctx->config ? ctx->config->integrity : WL_INTEGRITY_NONE,
      .flags = ctx->tx_last_flags,
      .cmd_id = ctx->tx_last_cmd_id,
      .session_id = ctx->session_id,
      .sequence = ctx->tx_retry_sequence,
      .payload = ctx->tx_payload.data,
      .payload_len = ctx->tx_payload.length,
  };
  size_t encoded_len = 0U;
  wl_sink_result_t sink_result;
  int ret;

  (void)retrying;

  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->config == NULL || ctx->sink == NULL) {
    return WL_ERR_NOT_INITIALIZED;
  }
  if (ctx->in_callback) {
    return WL_ERR_REENTRANT;
  }

  ret = wl_frame_encode(&wire, ctx->config->envelope, ctx->storage.tx_unit,
                       ctx->storage.tx_unit_size, &encoded_len);
  if (ret != WL_OK) {
    return ret;
  }

  if (ctx->tx_token == UINT32_MAX) {
    ctx->tx_token = 1U;
  } else {
    ctx->tx_token++;
  }

  ctx->in_callback = 1;
  sink_result = ctx->sink(ctx->sink_user_data, ctx->tx_token,
                          ctx->storage.tx_unit, encoded_len);
  ctx->in_callback = 0;

  if (sink_result == WL_SINK_STARTED) {
    ctx->tx_inflight = 1;
    ctx->tx_state = WL_TX_STATE_SENDING;
    ctx->tx_start_ts = ctx->now_ms;
    ctx->in_flight_reliable = ctx->tx_current_reliable;
    return WL_OK;
  }

  if (sink_result == WL_SINK_BUSY) {
    ctx->tx_inflight = 0;
    ctx->tx_queued = 1U;
    ctx->tx_state = WL_TX_STATE_SENDING;
    return WL_OK;
  }

  if (sink_result == WL_SINK_SENT) {
    ctx->tx_inflight = 0;
    ctx->tx_queued = 0U;
    if (ctx->tx_current_reliable) {
      ctx->tx_state = WL_TX_STATE_WAITING_ACK;
      ctx->tx_wait_state = WL_TX_WAIT_ACK;
      ctx->tx_start_ts = ctx->now_ms;
      return WL_OK;
    }

    ctx->tx_state = WL_TX_STATE_SUCCESS;
    ctx->tx_wait_state = WL_TX_WAIT_NONE;
    ctx->tx_waiting_seq = 0U;
    ctx->tx_retries_left = 0U;
    ctx->tx_current_reliable = 0U;
    ctx->tx_last_cmd_id = 0U;
    ctx->tx_last_flags = 0U;
    memset(ctx->storage.tx_payload, 0, ctx->storage.tx_payload_size);
    ctx->tx_payload.length = 0U;
    return wl_push_event(ctx, WL_EVT_TX_SUCCESS, 0U, NULL, 0U, 0U);
  }

  if (sink_result == WL_SINK_FAILED) {
    return WL_ERR_IO;
  }

  return WL_ERR_IO;
}

static void wl_prepare_tx_payload(wl_ctx_t *ctx, const wl_wire_packet_t *pkt,
                                 uint8_t reliable) {
  ctx->tx_last_cmd_id = pkt->cmd_id;
  ctx->tx_last_flags = pkt->flags;
  ctx->tx_retry_sequence = pkt->sequence;
  ctx->tx_current_reliable = reliable;
  ctx->tx_payload.data = ctx->storage.tx_payload;
  ctx->tx_payload.length = pkt->payload_len;

  if (pkt->payload_len != 0U) {
    memcpy(ctx->storage.tx_payload, pkt->payload, pkt->payload_len);
  }
}

int wl_tx_complete(wl_ctx_t *ctx, wl_io_token_t token, int io_result) {
  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->control_inflight != 0U && ctx->tx_token == token) {
    ctx->control_inflight = 0U;
    ctx->control_pending = 0U;
    return (io_result == WL_OK) ? WL_OK : WL_ERR_IO;
  }
  if (ctx->tx_token != token) {
    return WL_ERR_NOT_FOUND;
  }
  if (ctx->tx_cancel_requested != 0U) {
    ctx->tx_inflight = 0U;
    ctx->in_flight_reliable = 0U;
    ctx->tx_cancel_requested = 0U;
    return WL_OK;
  }
  if (ctx->tx_state != WL_TX_STATE_SENDING) {
    return WL_OK;
  }

  if (io_result == WL_OK) {
    if (ctx->in_flight_reliable) {
      ctx->tx_state = WL_TX_STATE_WAITING_ACK;
      ctx->tx_wait_state = WL_TX_WAIT_ACK;
      ctx->tx_start_ts = ctx->now_ms;
    } else {
      ctx->tx_state = WL_TX_STATE_SUCCESS;
      ctx->tx_wait_state = WL_TX_WAIT_NONE;
      ctx->tx_waiting_seq = 0U;
      (void)wl_push_event(ctx, WL_EVT_TX_SUCCESS, 0U, NULL, 0U, 0U);
    }
    ctx->tx_inflight = 0;
    ctx->in_flight_reliable = 0U;
    (void)wl_submit_control(ctx);
    return WL_OK;
  }

  if (ctx->in_flight_reliable) {
    if (ctx->tx_retries_left != 0U) {
      ctx->tx_inflight = 0U;
      int retry = wl_send_tx_payload(ctx, 1U);
      if (retry == WL_OK) {
        ctx->tx_retries_left--;
        ctx->tx_retries_used++;
        return WL_OK;
      }
      ctx->tx_state = WL_TX_STATE_FAILED;
      ctx->tx_wait_state = WL_TX_WAIT_NONE;
      ctx->tx_waiting_seq = 0U;
      ctx->tx_result_code = WL_ERR_IO;
      (void)wl_push_event(ctx, WL_EVT_TX_FAILED, 0U, NULL, 0U, ctx->tx_handle);
      return WL_OK;
    }
    ctx->tx_state = WL_TX_STATE_FAILED;
    ctx->tx_wait_state = WL_TX_WAIT_NONE;
    ctx->tx_waiting_seq = 0U;
    ctx->tx_result_code = WL_ERR_IO;
    (void)wl_push_event(ctx, WL_EVT_TX_FAILED, 0U, NULL, 0U, ctx->tx_handle);
  } else {
    ctx->tx_state = WL_TX_STATE_IDLE;
  }

  ctx->tx_wait_state = WL_TX_WAIT_NONE;
  ctx->tx_waiting_seq = 0U;
  ctx->tx_inflight = 0;
  ctx->in_flight_reliable = 0U;
  (void)wl_submit_control(ctx);
  return WL_OK;
}

static int wl_send_ack(wl_ctx_t *ctx, const wl_frame_view_t *view) {
  size_t encoded_len = 0;
  wl_wire_packet_t ack = {0};

  if (ctx == NULL || view == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->config == NULL || ctx->sink == NULL) {
    return WL_ERR_NOT_INITIALIZED;
  }
  if (ctx->in_callback) {
    return WL_ERR_REENTRANT;
  }

  ack.type = WL_PACKET_ACK;
  ack.flags = 0U;
  ack.cmd_id = 0U;
  ack.session_id = view->session_id;
  ack.sequence = view->sequence;
  ack.payload = NULL;
  ack.payload_len = 0U;
  ack.integrity = ctx->config->integrity;

  if (ctx->control_pending != 0U) {
    return WL_ERR_QUEUE_FULL;
  }
  int ret = wl_frame_encode(&ack, ctx->config->envelope, ctx->storage.control_unit,
                            ctx->storage.control_unit_size, &encoded_len);
  if (ret != WL_OK) {
    return ret;
  }
  ctx->control_len = encoded_len;
  ctx->control_pending = 1U;
  return wl_submit_control(ctx);
}

static int wl_submit_control(wl_ctx_t *ctx) {
  wl_sink_result_t sink_result;
  if (ctx == NULL || ctx->control_pending == 0U ||
      ctx->control_inflight != 0U || ctx->tx_inflight != 0U) {
    return WL_OK;
  }
  if (ctx->sink == NULL) {
    return WL_ERR_NOT_INITIALIZED;
  }
  if (ctx->in_callback) {
    return WL_ERR_REENTRANT;
  }
  if (ctx->tx_token == UINT32_MAX) {
    ctx->tx_token = 1U;
  } else {
    ctx->tx_token++;
  }
  ctx->in_callback = 1;
  sink_result = ctx->sink(ctx->sink_user_data, ctx->tx_token,
                          ctx->storage.control_unit, ctx->control_len);
  ctx->in_callback = 0;
  if (sink_result == WL_SINK_SENT) {
    ctx->control_pending = 0U;
    return WL_OK;
  }
  if (sink_result == WL_SINK_STARTED) {
    ctx->control_inflight = 1U;
    return WL_OK;
  }
  if (sink_result == WL_SINK_BUSY) {
    return WL_OK;
  }
  ctx->control_pending = 0U;
  return WL_ERR_IO;
}

static int wl_handle_ack(wl_ctx_t *ctx, const wl_frame_view_t *view) {
  if (ctx == NULL || view == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->tx_wait_state != WL_TX_WAIT_ACK ||
      ctx->tx_state != WL_TX_STATE_WAITING_ACK) {
    return WL_OK;
  }

  if (ctx->session_id != view->session_id ||
      ctx->tx_waiting_seq != view->sequence) {
    return WL_OK;
  }

  ctx->tx_state = WL_TX_STATE_SUCCESS;
  ctx->tx_wait_state = WL_TX_WAIT_NONE;
  ctx->tx_waiting_seq = 0U;
  ctx->tx_retries_left = 0U;
  ctx->tx_result_code = WL_OK;
  return wl_push_event(ctx, WL_EVT_TX_SUCCESS, 0U, NULL, 0U, ctx->tx_handle);
}

static int wl_send_frame_internal(wl_ctx_t *ctx, const wl_wire_packet_t *pkt,
                                 wl_tx_handle_t *out_handle,
                                 uint8_t reliable) {
  wl_tx_handle_t generated_handle;

  if (ctx == NULL || pkt == NULL || (pkt->payload_len != 0U && pkt->payload == NULL)) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->session_id == 0ULL) {
    /* session_id=0 is reserved by protocol; fail fast before serialization. */
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->config == NULL) {
    return WL_ERR_NOT_INITIALIZED;
  }
  if (pkt->payload_len > ctx->config->max_payload_len) {
    return WL_ERR_PAYLOAD_TOO_LONG;
  }
  if (ctx->tx_wait_state == WL_TX_WAIT_ACK || ctx->tx_handle != 0U ||
      ctx->tx_inflight != 0U ||
      ctx->tx_queued != 0U) {
    return WL_ERR_BUSY;
  }
  if (ctx->in_callback) {
    return WL_ERR_REENTRANT;
  }

  if (pkt->payload_len > ctx->config->max_payload_len) {
    return WL_ERR_PAYLOAD_TOO_LONG;
  }

  wl_prepare_tx_payload(ctx, pkt, reliable);

  generated_handle = 0U;
  if (reliable != 0U) {
    ctx->tx_generation++;
    if (ctx->tx_generation == 0U) {
      ctx->tx_generation = 1U;
    }
    generated_handle = ((uint32_t)ctx->tx_generation << 16U) | 1U;
    ctx->tx_handle = generated_handle;
  }
  ctx->tx_waiting_seq = reliable ? pkt->sequence : 0U;
  ctx->tx_wait_state = WL_TX_WAIT_NONE;
  ctx->tx_retries_left = reliable ? ctx->tx_retries_max : 0U;
  ctx->tx_retries_used = 0U;

  int ret = wl_send_tx_payload(ctx, 0U);
  if (ret != WL_OK) {
    if (reliable != 0U) {
      ctx->tx_handle = 0U;
    }
    if (out_handle != NULL) {
      *out_handle = 0U;
    }
    ctx->tx_waiting_seq = 0U;
    ctx->tx_wait_state = WL_TX_WAIT_NONE;
    ctx->tx_retries_left = 0U;
    memset(ctx->storage.tx_payload, 0, ctx->storage.tx_payload_size);
    ctx->tx_payload.length = 0U;
    ctx->tx_last_cmd_id = 0U;
    ctx->tx_last_flags = 0U;
    ctx->tx_current_reliable = 0U;
    ctx->tx_retry_sequence = 0U;
    return ret;
  }

  if (reliable) {
    ctx->tx_waiting_seq = pkt->sequence;
    ctx->tx_result_code = WL_ERR_BUSY;
  }

  if (out_handle != NULL) {
    *out_handle = generated_handle;
    return WL_OK;
  }

  return WL_OK;
}

int wl_send_unreliable(wl_ctx_t *ctx, uint16_t cmd_id, const uint8_t *payload,
                       size_t payload_len) {
  wl_wire_packet_t pkt;

  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->tx_sequence == UINT32_MAX) {
    return WL_ERR_INVALID_STATE;
  }

  memset(&pkt, 0, sizeof(pkt));
  pkt.type = WL_PACKET_DATA;
  pkt.flags = 0U;
  pkt.cmd_id = cmd_id;
  pkt.sequence = ctx->tx_sequence++;
  pkt.payload = payload;
  pkt.payload_len = payload_len;

  return wl_send_frame_internal(ctx, &pkt, NULL, 0U);
}

int wl_send_reliable(wl_ctx_t *ctx, uint16_t cmd_id, const uint8_t *payload,
                     size_t payload_len, wl_tx_handle_t *out_handle) {
  wl_wire_packet_t pkt;

  if (ctx == NULL || out_handle == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->tx_sequence == UINT32_MAX) {
    return WL_ERR_INVALID_STATE;
  }

  memset(&pkt, 0, sizeof(pkt));
  pkt.type = WL_PACKET_DATA;
  pkt.flags = WL_PACKET_FLAG_RELIABLE;
  pkt.cmd_id = cmd_id;
  pkt.sequence = ctx->tx_sequence++;
  pkt.payload = payload;
  pkt.payload_len = payload_len;

  *out_handle = 0;
  return wl_send_frame_internal(ctx, &pkt, out_handle, 1U);
}

static int wl_feed_parse_wire(wl_ctx_t *ctx, const uint8_t *data, size_t len) {
  wl_frame_view_t view;
  int ret;

  if (ctx == NULL || data == NULL || len == 0U) {
    return WL_ERR_INVALID_ARG;
  }

  ret = wl_frame_decode(data, len, ctx->config ? ctx->config->integrity :
                                       WL_INTEGRITY_NONE,
                       &view);
  if (ret != WL_OK) {
    if (ret == WL_ERR_CRC) {
      ctx->rx_counters.bad_integrity++;
    } else if (ret == WL_ERR_NOT_SUPPORTED) {
      ctx->rx_counters.unsupported++;
    } else {
      ctx->rx_counters.malformed++;
    }
    return ret;
  }

  if (view.payload.length > ctx->config->max_payload_len) {
    return WL_ERR_PAYLOAD_TOO_LONG;
  }

  switch (view.type) {
  case WL_PACKET_DATA:
    if ((view.flags & WL_PACKET_FLAG_RELIABLE) != 0U) {
      if (ctx->rx_have_reliable != 0U &&
          ctx->rx_session_id == view.session_id &&
          ctx->seq_recv == view.sequence) {
        ctx->rx_counters.duplicate++;
        return wl_send_ack(ctx, &view);
      }
      ret = wl_push_event(ctx, WL_EVT_RELIABLE_RX, view.cmd_id, view.payload.data,
                          view.payload.length, 0U);
      if (ret != WL_OK) {
        return ret;
      }
      ctx->rx_session_id = view.session_id;
      ctx->seq_recv = view.sequence;
      ctx->rx_have_reliable = 1U;
      return wl_send_ack(ctx, &view);
    }
    return wl_push_event(ctx, WL_EVT_UNRELIABLE_RX, view.cmd_id, view.payload.data,
                        view.payload.length, 0U);
  case WL_PACKET_ACK:
    return wl_handle_ack(ctx, &view);
  case WL_PACKET_NACK:
    ctx->rx_counters.unsupported++;
    return WL_ERR_NOT_SUPPORTED;
  default:
    return WL_OK;
  }
}

int wl_feed_unit(wl_ctx_t *ctx, const uint8_t *unit, size_t len) {
  size_t accepted = 0U;
  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (unit == NULL && len != 0U) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->config == NULL) {
    return WL_ERR_NOT_INITIALIZED;
  }

  if (ctx->config->envelope == WL_ENVELOPE_COBS_STREAM) {
    return wl_feed_bytes(ctx, unit, len, &accepted);
  }

  if (ctx->has_event != 0U || ctx->rx_event_leased != 0U) {
    return WL_ERR_WOULD_BLOCK;
  }
  if (len > ctx->storage.rx_fallback_size) {
    return WL_ERR_PAYLOAD_TOO_LONG;
  }
  if (len != 0U) {
    memcpy(ctx->storage.rx_fallback, unit, len);
  }
  ctx->rx_candidate_source = WL_RX_SOURCE_FALLBACK;
  return wl_feed_unit_raw(ctx, ctx->storage.rx_fallback, len);
}

static int wl_feed_unit_raw(wl_ctx_t *ctx, const uint8_t *unit, size_t len) {
  size_t raw_len;
  if (ctx == NULL || unit == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->config->envelope == WL_ENVELOPE_BUS_LENGTH16) {
    if (len < 2U) {
      ctx->rx_counters.malformed++;
      return WL_ERR_BAD_FRAME;
    }
    raw_len = ((size_t)unit[0] << 8U) | unit[1];
    if (raw_len + 2U > len ||
        (ctx->config->max_transmission_unit != 0U &&
         raw_len + 2U > ctx->config->max_transmission_unit)) {
      ctx->rx_counters.malformed++;
      return WL_ERR_BAD_FRAME;
    }
    for (size_t i = raw_len + 2U; i < len; ++i) {
      if (unit[i] != 0U) {
        ctx->rx_counters.malformed++;
        return WL_ERR_BAD_FRAME;
      }
    }
    return wl_feed_parse_wire(ctx, unit + 2U, raw_len);
  }
  return wl_feed_parse_wire(ctx, unit, len);
}

int wl_rx_reserve(wl_ctx_t *ctx, wl_span_t *out_span) {
  if (ctx == NULL || out_span == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->config == NULL) {
    return WL_ERR_NOT_INITIALIZED;
  }
  if (ctx->config->envelope != WL_ENVELOPE_COBS_STREAM) {
    return WL_ERR_NOT_SUPPORTED;
  }
  return wl_rx_ring_producer_reserve(&ctx->rx_ring, out_span);
}

int wl_rx_commit(wl_ctx_t *ctx, size_t len) {
  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->config == NULL) {
    return WL_ERR_NOT_INITIALIZED;
  }
  if (ctx->config->envelope != WL_ENVELOPE_COBS_STREAM) {
    return WL_ERR_NOT_SUPPORTED;
  }
  return wl_rx_ring_producer_commit(&ctx->rx_ring, len);
}

int wl_feed_bytes(wl_ctx_t *ctx, const uint8_t *data, size_t len,
                  size_t *out_accepted) {
  size_t accepted = 0U;

  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if ((data == NULL && len != 0U) || out_accepted == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  *out_accepted = 0U;
  if (ctx->config == NULL) {
    return WL_ERR_NOT_INITIALIZED;
  }
  if (ctx->config->envelope != WL_ENVELOPE_COBS_STREAM) {
    return WL_ERR_NOT_SUPPORTED;
  }

  while (accepted < len) {
    wl_span_t span = {0};
    size_t chunk;
    int ret = wl_rx_reserve(ctx, &span);
    if (ret != WL_OK) {
      *out_accepted = accepted;
      return ret;
    }
    if (span.length == 0U) {
      (void)wl_rx_commit(ctx, 0U);
      break;
    }
    chunk = len - accepted;
    if (chunk > span.length) {
      chunk = span.length;
    }
    memcpy(span.data, data + accepted, chunk);
    ret = wl_rx_commit(ctx, chunk);
    if (ret != WL_OK) {
      *out_accepted = accepted;
      return ret;
    }
    accepted += chunk;
  }

  *out_accepted = accepted;
  if (accepted != len) {
    wl_rx_ring_producer_note_overflow(&ctx->rx_ring);
    return WL_ERR_WOULD_BLOCK;
  }
  return WL_OK;
}

static void wl_process_rx_stream(wl_ctx_t *ctx) {
  unsigned int overflow_events =
      wl_rx_ring_consumer_take_overflow(&ctx->rx_ring);

  if (overflow_events != 0U) {
    size_t readable = wl_rx_ring_readable(&ctx->rx_ring);
    uint32_t remaining = UINT32_MAX - ctx->rx_counters.overflow;
    uint32_t increment = overflow_events > remaining
                             ? remaining
                             : (uint32_t)overflow_events;

    ctx->rx_counters.overflow += increment;
    (void)wl_rx_ring_consumer_consume(&ctx->rx_ring, readable);
  }

  while (ctx->has_event == 0U && ctx->rx_event_leased == 0U) {
    wl_span_t contiguous;
    uint8_t *encoded;
    size_t delimiter_offset;
    size_t decoded_len = 0U;
    size_t physical_len;
    int ret;

    ret = wl_rx_ring_consumer_find(&ctx->rx_ring, 0U, &delimiter_offset);
    if (ret != WL_OK) {
      return;
    }
    physical_len = delimiter_offset + 1U;
    if (delimiter_offset == 0U) {
      (void)wl_rx_ring_consumer_consume(&ctx->rx_ring, physical_len);
      continue;
    }

    contiguous = wl_rx_ring_consumer_peek(&ctx->rx_ring);
    if (delimiter_offset <= contiguous.length) {
      encoded = contiguous.data;
      ctx->rx_candidate_source = WL_RX_SOURCE_RING;
    } else {
      if (delimiter_offset > ctx->storage.rx_fallback_size ||
          wl_rx_ring_consumer_copy(&ctx->rx_ring, 0U,
                                   ctx->storage.rx_fallback,
                                   delimiter_offset) != WL_OK) {
        ctx->rx_counters.overflow++;
        (void)wl_rx_ring_consumer_consume(&ctx->rx_ring, physical_len);
        continue;
      }
      encoded = ctx->storage.rx_fallback;
      ctx->rx_candidate_source = WL_RX_SOURCE_FALLBACK;
    }

    ret = wl_cobs_decode_in_place(encoded, delimiter_offset, &decoded_len);
    if (ret == WL_OK) {
      ret = wl_feed_parse_wire(ctx, encoded, decoded_len);
    } else {
      ctx->rx_counters.malformed++;
    }

    if (ctx->has_event != 0U &&
        (ctx->event.type == WL_EVT_UNRELIABLE_RX ||
         ctx->event.type == WL_EVT_RELIABLE_RX)) {
      if (ctx->rx_candidate_source == WL_RX_SOURCE_RING) {
        ctx->rx_pending_consume = physical_len;
      } else {
        (void)wl_rx_ring_consumer_consume(&ctx->rx_ring, physical_len);
        ctx->rx_pending_consume = 0U;
      }
      return;
    }

    (void)wl_rx_ring_consumer_consume(&ctx->rx_ring, physical_len);
    ctx->rx_candidate_source = WL_RX_SOURCE_NONE;
    if (ctx->has_event != 0U) {
      return;
    }
    (void)ret;
  }
}

int wl_poll(wl_ctx_t *ctx, wl_time_ms_t now_ms, wl_event_t *out_event) {
  if (ctx == NULL || out_event == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  ctx->now_ms = now_ms;

  (void)wl_submit_control(ctx);
  if (ctx->control_pending == 0U && ctx->tx_state == WL_TX_STATE_SENDING &&
      ctx->tx_queued != 0U && ctx->tx_inflight == 0U) {
    (void)wl_send_tx_payload(ctx, 0U);
  }

  if (ctx->control_pending == 0U && ctx->tx_state == WL_TX_STATE_WAITING_ACK &&
      ctx->config != NULL && ctx->config->ack_timeout_ms != 0U) {
    wl_time_ms_t timeout_ms = now_ms - ctx->tx_start_ts;
    if (timeout_ms >= ctx->config->ack_timeout_ms) {
      if (ctx->tx_retries_left == 0U) {
        ctx->tx_state = WL_TX_STATE_FAILED;
        ctx->tx_wait_state = WL_TX_WAIT_NONE;
        ctx->tx_waiting_seq = 0U;
        ctx->tx_result_code = WL_ERR_TIMEOUT;
        (void)wl_push_event(ctx, WL_EVT_TX_TIMEOUT, 0U, NULL, 0U, ctx->tx_handle);
      } else {
        int retry = wl_send_tx_payload(ctx, 1U);
        if (retry == WL_OK) {
          ctx->tx_retries_left--;
          ctx->tx_retries_used++;
          if (ctx->tx_state == WL_TX_STATE_WAITING_ACK) {
            ctx->tx_start_ts = now_ms;
          }
        } else {
          ctx->tx_state = WL_TX_STATE_FAILED;
          ctx->tx_wait_state = WL_TX_WAIT_NONE;
          ctx->tx_waiting_seq = 0U;
          ctx->tx_result_code = WL_ERR_IO;
          (void)wl_push_event(ctx, WL_EVT_TX_FAILED, 0U, NULL, 0U,
                             ctx->tx_handle);
        }
      }
    }
  }

  if (ctx->has_event == 0U && ctx->rx_event_leased == 0U &&
      ctx->config != NULL &&
      ctx->config->envelope == WL_ENVELOPE_COBS_STREAM) {
    wl_process_rx_stream(ctx);
  }

  if (!ctx->has_event) {
    return WL_ERR_NO_DATA;
  }

  *out_event = ctx->event;
  ctx->has_event = 0;
  if (out_event->type == WL_EVT_UNRELIABLE_RX ||
      out_event->type == WL_EVT_RELIABLE_RX) {
    ctx->rx_event_leased = 1U;
  }

  return WL_OK;
}
