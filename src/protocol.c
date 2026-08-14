/* SPDX-License-Identifier: Apache-2.0 */

#include <limits.h>
#include <string.h>

#include "wirelink/cobs.h"
#include "wirelink/wirelink.h"

static int wl_feed_unit_raw(wl_ctx_t *ctx, const uint8_t *unit, size_t len);
static int wl_send_ack(wl_ctx_t *ctx, const wl_frame_view_t *view);
static int wl_handle_ack(wl_ctx_t *ctx, const wl_frame_view_t *view);
static int wl_send_tx_payload(wl_ctx_t *ctx, uint8_t retrying);
static void wl_prepare_tx_payload(wl_ctx_t *ctx, const wl_wire_packet_t *pkt,
                                 uint8_t reliable);

static int wl_push_event(wl_ctx_t *ctx, wl_event_type_t type, uint16_t cmd_id,
                        const uint8_t *payload, size_t payload_len,
                        wl_tx_handle_t handle) {
  if (ctx->has_event) {
    return WL_ERR_QUEUE_FULL;
  }

  ctx->event.type = type;
  ctx->event.cmd_id = cmd_id;
  ctx->event.payload = payload;
  ctx->event.payload_len = payload_len;
  ctx->event.handle = handle;
  ctx->event.io_result = 0;
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
  uint8_t encoded[WL_FRAME_MAX_COBS_LEN];
  size_t encoded_len = 0U;
  wl_sink_result_t sink_result;
  int ret;

  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->config == NULL || ctx->sink == NULL) {
    return WL_ERR_NOT_INITIALIZED;
  }
  if (ctx->in_callback) {
    return WL_ERR_REENTRANT;
  }

  ret = wl_frame_encode(&wire, ctx->config->envelope, encoded,
                       sizeof(encoded), &encoded_len);
  if (ret != WL_OK) {
    return ret;
  }

  if (ctx->tx_token == UINT32_MAX) {
    ctx->tx_token = 1U;
  } else {
    ctx->tx_token++;
  }

  ctx->in_callback = 1;
  sink_result = ctx->sink(ctx->sink_user_data, ctx->tx_token, encoded, encoded_len);
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
    if (!ctx->tx_current_reliable || retrying == 0U) {
      ctx->tx_state = WL_TX_STATE_IDLE;
      ctx->tx_waiting_ack = 0U;
      ctx->tx_waiting_seq = 0U;
      ctx->tx_retries_left = 0U;
      memset(ctx->tx_payload_storage, 0, sizeof(ctx->tx_payload_storage));
      ctx->tx_payload.length = 0U;
      ctx->tx_current_reliable = 0U;
      ctx->tx_last_cmd_id = 0U;
      ctx->tx_last_flags = 0U;
      ctx->tx_retry_sequence = 0U;
      return WL_ERR_WOULD_BLOCK;
    }

    if (retrying != 0U && ctx->tx_current_reliable != 0U) {
      ctx->tx_state = WL_TX_STATE_WAITING_ACK;
      ctx->tx_start_ts = ctx->now_ms;
      return WL_ERR_WOULD_BLOCK;
    }
    return WL_ERR_WOULD_BLOCK;
  }

  if (sink_result == WL_SINK_SENT) {
    ctx->tx_inflight = 0;
    if (ctx->tx_current_reliable) {
      ctx->tx_state = WL_TX_STATE_WAITING_ACK;
      ctx->tx_start_ts = ctx->now_ms;
      return WL_OK;
    }

    ctx->tx_state = WL_TX_STATE_SUCCESS;
    ctx->tx_waiting_ack = 0U;
    ctx->tx_waiting_seq = 0U;
    ctx->tx_retries_left = 0U;
    ctx->tx_current_reliable = 0U;
    ctx->tx_last_cmd_id = 0U;
    ctx->tx_last_flags = 0U;
    memset(ctx->tx_payload_storage, 0, sizeof(ctx->tx_payload_storage));
    ctx->tx_payload.length = 0U;
    return wl_push_event(ctx, WL_EVT_TX_SUCCESS, 0U, NULL, 0U, ctx->tx_handle);
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
  ctx->tx_payload.data = ctx->tx_payload_storage;
  ctx->tx_payload.length = pkt->payload_len;

  if (pkt->payload_len != 0U) {
    memcpy(ctx->tx_payload_storage, pkt->payload, pkt->payload_len);
  }
}

int wl_tx_complete(wl_ctx_t *ctx, wl_io_token_t token, int io_result) {
  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->tx_token != token) {
    return WL_ERR_NOT_FOUND;
  }
  if (ctx->tx_state != WL_TX_STATE_SENDING) {
    return WL_OK;
  }

  if (io_result == WL_OK) {
    if (ctx->in_flight_reliable) {
      ctx->tx_state = WL_TX_STATE_WAITING_ACK;
      ctx->tx_waiting_ack = 1U;
    } else {
      ctx->tx_state = WL_TX_STATE_SUCCESS;
      ctx->tx_waiting_ack = 0U;
      ctx->tx_waiting_seq = 0U;
      (void)wl_push_event(ctx, WL_EVT_TX_SUCCESS, 0U, NULL, 0U, ctx->tx_handle);
    }
    ctx->tx_inflight = 0;
    ctx->in_flight_reliable = 0U;
    return WL_OK;
  }

  if (ctx->in_flight_reliable) {
    if (ctx->tx_retries_left != 0U) {
      int retry = wl_send_tx_payload(ctx, 1U);
      if (retry == WL_ERR_WOULD_BLOCK) {
        ctx->tx_state = WL_TX_STATE_WAITING_ACK;
        ctx->tx_start_ts = ctx->now_ms;
        return WL_OK;
      }
      if (retry == WL_OK) {
        ctx->tx_retries_left--;
        return WL_OK;
      }
      ctx->tx_state = WL_TX_STATE_FAILED;
      ctx->tx_waiting_ack = 0U;
      ctx->tx_waiting_seq = 0U;
      (void)wl_push_event(ctx, WL_EVT_TX_TIMEOUT, 0U, NULL, 0U, ctx->tx_handle);
      return WL_OK;
    }
    ctx->tx_state = WL_TX_STATE_FAILED;
    ctx->tx_waiting_ack = 0U;
    ctx->tx_waiting_seq = 0U;
    (void)wl_push_event(ctx, WL_EVT_TX_TIMEOUT, 0U, NULL, 0U, ctx->tx_handle);
  } else {
    ctx->tx_state = WL_TX_STATE_IDLE;
  }

  ctx->tx_waiting_ack = 0U;
  ctx->tx_waiting_seq = 0U;
  ctx->tx_inflight = 0;
  ctx->in_flight_reliable = 0U;
  return WL_OK;
}

static int wl_send_ack(wl_ctx_t *ctx, const wl_frame_view_t *view) {
  uint8_t encoded[WL_FRAME_MAX_COBS_LEN];
  size_t encoded_len = 0;
  wl_wire_packet_t ack = {0};
  wl_sink_result_t sink_result;

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

  int ret = wl_frame_encode(&ack, ctx->config->envelope, encoded,
                           sizeof(encoded), &encoded_len);
  if (ret != WL_OK) {
    return ret;
  }

  ctx->in_callback = 1;
  sink_result = ctx->sink(ctx->sink_user_data, ctx->tx_token, encoded, encoded_len);
  ctx->in_callback = 0;

  if (sink_result == WL_SINK_SENT) {
    return WL_OK;
  }
  if (sink_result == WL_SINK_BUSY || sink_result == WL_SINK_STARTED) {
    return WL_ERR_WOULD_BLOCK;
  }
  return WL_ERR_IO;
}

static int wl_handle_ack(wl_ctx_t *ctx, const wl_frame_view_t *view) {
  if (ctx == NULL || view == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (!ctx->tx_waiting_ack || ctx->tx_state != WL_TX_STATE_WAITING_ACK) {
    return WL_OK;
  }

  if (ctx->session_id != view->session_id ||
      ctx->tx_waiting_seq != view->sequence) {
    return WL_OK;
  }

  ctx->tx_state = WL_TX_STATE_SUCCESS;
  ctx->tx_waiting_ack = 0U;
  ctx->tx_waiting_seq = 0U;
  ctx->tx_retries_left = 0U;
  return wl_push_event(ctx, WL_EVT_TX_SUCCESS, 0U, NULL, 0U, ctx->tx_handle);
}

static int wl_send_frame_internal(wl_ctx_t *ctx, const wl_wire_packet_t *pkt,
                                 wl_tx_handle_t *out_handle,
                                 uint8_t reliable) {
  wl_tx_handle_t generated_handle;

  if (ctx == NULL || pkt == NULL || (pkt->payload_len != 0U && pkt->payload == NULL)) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->config == NULL) {
    return WL_ERR_NOT_INITIALIZED;
  }
  if (pkt->payload_len > ctx->config->max_payload_len) {
    return WL_ERR_PAYLOAD_TOO_LONG;
  }
  if (ctx->tx_inflight) {
    return WL_ERR_BUSY;
  }
  if (ctx->in_callback) {
    return WL_ERR_REENTRANT;
  }

  if (pkt->payload_len > sizeof(ctx->tx_payload_storage)) {
    return WL_ERR_PAYLOAD_TOO_LONG;
  }

  wl_prepare_tx_payload(ctx, pkt, reliable);

  generated_handle = ctx->tx_next_handle;
  ctx->tx_next_handle++;
  if (ctx->tx_next_handle == 0U) {
    ctx->tx_next_handle = 1U;
  }
  ctx->tx_handle = generated_handle;
  ctx->tx_waiting_seq = reliable ? pkt->sequence : 0U;
  ctx->tx_waiting_ack = reliable ? 1U : 0U;
  ctx->tx_retries_left = reliable ? ctx->tx_retries_max : 0U;

  int ret = wl_send_tx_payload(ctx, 0U);
  if (ret != WL_OK) {
    ctx->tx_handle = 0U;
    if (out_handle != NULL) {
      *out_handle = 0U;
    }
    ctx->tx_waiting_seq = 0U;
    ctx->tx_waiting_ack = 0U;
    ctx->tx_retries_left = 0U;
    memset(ctx->tx_payload_storage, 0, sizeof(ctx->tx_payload_storage));
    ctx->tx_payload.length = 0U;
    ctx->tx_last_cmd_id = 0U;
    ctx->tx_last_flags = 0U;
    ctx->tx_current_reliable = 0U;
    ctx->tx_retry_sequence = 0U;
    return ret;
  }

  if (reliable) {
    ctx->tx_start_ts = ctx->now_ms;
    ctx->tx_waiting_seq = pkt->sequence;
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
    return ret;
  }

  if (view.payload.length > ctx->config->max_payload_len) {
    return WL_ERR_PAYLOAD_TOO_LONG;
  }

  switch (view.type) {
  case WL_PACKET_DATA:
    if ((view.flags & WL_PACKET_FLAG_RELIABLE) != 0U) {
      if (ctx->has_event) {
        return WL_ERR_QUEUE_FULL;
      }
      (void)wl_send_ack(ctx, &view);
      return wl_push_event(ctx, WL_EVT_RELIABLE_RX, view.cmd_id, view.payload.data,
                          view.payload.length, 0U);
    }
    return wl_push_event(ctx, WL_EVT_UNRELIABLE_RX, view.cmd_id, view.payload.data,
                        view.payload.length, 0U);
  case WL_PACKET_ACK:
    return wl_handle_ack(ctx, &view);
  case WL_PACKET_NACK:
  default:
    return WL_OK;
  }
}

int wl_feed_unit(wl_ctx_t *ctx, const uint8_t *unit, size_t len) {
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
    return wl_feed_bytes(ctx, unit, len);
  }

  return wl_feed_unit_raw(ctx, unit, len);
}

static int wl_feed_unit_raw(wl_ctx_t *ctx, const uint8_t *unit, size_t len) {
  if (ctx == NULL || unit == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  return wl_feed_parse_wire(ctx, unit, len);
}

int wl_feed_bytes(wl_ctx_t *ctx, const uint8_t *data, size_t len) {
  size_t idx = 0;
  if (ctx == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (data == NULL && len != 0U) {
    return WL_ERR_INVALID_ARG;
  }
  if (ctx->config == NULL) {
    return WL_ERR_NOT_INITIALIZED;
  }
  if (ctx->config->envelope != WL_ENVELOPE_COBS_STREAM) {
    return wl_feed_unit_raw(ctx, data, len);
  }

  while (idx < len) {
    uint8_t byte = data[idx++];

    if (byte == 0x00) {
      if (ctx->cobs_accum_len == 0U) {
        continue;
      }
      if (ctx->cobs_overflow) {
        ctx->cobs_accum_len = 0;
        ctx->cobs_overflow = 0;
        continue;
      }

      uint8_t decoded[WL_FRAME_MAX_COBS_LEN];
      size_t decoded_len = 0;
      int ret = wl_cobs_decode(ctx->cobs_accum, ctx->cobs_accum_len, decoded,
                               sizeof(decoded), &decoded_len);
      if (ret == WL_OK) {
        (void)wl_feed_parse_wire(ctx, decoded, decoded_len);
      }
      ctx->cobs_accum_len = 0;
      continue;
    }

    if (!ctx->cobs_overflow) {
      if (ctx->cobs_accum_len < sizeof(ctx->cobs_accum)) {
        ctx->cobs_accum[ctx->cobs_accum_len++] = byte;
      } else {
        ctx->cobs_overflow = 1;
      }
    }
  }

  return WL_OK;
}

int wl_poll(wl_ctx_t *ctx, wl_time_ms_t now_ms, wl_event_t *out_event) {
  if (ctx == NULL || out_event == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  ctx->now_ms = now_ms;

  if (ctx->tx_state == WL_TX_STATE_WAITING_ACK &&
      ctx->config != NULL && ctx->config->ack_timeout_ms != 0U) {
    wl_time_ms_t timeout_ms = now_ms - ctx->tx_start_ts;
    if (timeout_ms >= ctx->config->ack_timeout_ms) {
      if (ctx->tx_retries_left == 0U) {
        ctx->tx_state = WL_TX_STATE_FAILED;
        ctx->tx_waiting_ack = 0U;
        ctx->tx_waiting_seq = 0U;
        (void)wl_push_event(ctx, WL_EVT_TX_TIMEOUT, 0U, NULL, 0U, ctx->tx_handle);
      } else {
        int retry = wl_send_tx_payload(ctx, 1U);
        if (retry == WL_ERR_WOULD_BLOCK) {
          ctx->tx_state = WL_TX_STATE_WAITING_ACK;
          ctx->tx_start_ts = now_ms;
        } else if (retry == WL_OK) {
          ctx->tx_retries_left--;
          ctx->tx_state = WL_TX_STATE_WAITING_ACK;
          ctx->tx_start_ts = now_ms;
        } else {
          ctx->tx_state = WL_TX_STATE_FAILED;
          ctx->tx_waiting_ack = 0U;
          ctx->tx_waiting_seq = 0U;
          (void)wl_push_event(ctx, WL_EVT_TX_TIMEOUT, 0U, NULL, 0U,
                             ctx->tx_handle);
        }
      }
    }
  }

  if (!ctx->has_event) {
    return WL_ERR_NO_DATA;
  }

  *out_event = ctx->event;
  ctx->has_event = 0;

  if (out_event->type == WL_EVT_TX_SUCCESS || out_event->type == WL_EVT_TX_TIMEOUT) {
    ctx->tx_state = WL_TX_STATE_IDLE;
  }

  return WL_OK;
}
