#ifndef INCLUDE_WIRELINK_WIRELINK_H_
#define INCLUDE_WIRELINK_WIRELINK_H_

#include <stddef.h>
#include <stdint.h>

#include "wirelink/bipbuf.h"
#include "wirelink/frame.h"
#include "wirelink/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t wl_time_ms_t;
typedef uint16_t wl_tx_handle_t;
typedef uint32_t wl_io_token_t;
typedef struct wl_ctx wl_ctx_t;

typedef enum {
  WL_TX_STATE_IDLE = 0,
  WL_TX_STATE_QUEUED,
  WL_TX_STATE_SENDING,
  WL_TX_STATE_WAITING_ACK,
  WL_TX_STATE_SUCCESS,
  WL_TX_STATE_FAILED,
  WL_TX_STATE_CANCELLED,
} wl_tx_state_t;

typedef enum {
  WL_EVT_NONE = 0,
  WL_EVT_UNRELIABLE_RX,
  WL_EVT_RELIABLE_RX,
  WL_EVT_TX_SUCCESS,
  WL_EVT_TX_TIMEOUT,
} wl_event_type_t;

typedef struct {
  wl_event_type_t type;
  uint16_t cmd_id;
  const uint8_t *payload;
  size_t payload_len;
  wl_tx_handle_t handle;
  int io_result;
} wl_event_t;

typedef enum {
  WL_SINK_SENT = 0,
  WL_SINK_STARTED,
  WL_SINK_BUSY,
  WL_SINK_FAILED,
} wl_sink_result_t;

typedef wl_sink_result_t (*wl_sink_fn)(void *user_data, wl_io_token_t token,
                                       const uint8_t *data, size_t len);

typedef struct {
  size_t rx_buf_size;
  size_t tx_buf_size;
  uint16_t max_payload_len;
  wl_envelope_type_t envelope;
  wl_integrity_t integrity;
  uint64_t session_id;
  uint16_t max_retries;
  uint32_t ack_timeout_ms;
} wl_config_t;

typedef struct wl_ctx {
  wl_bipbuf_t rx_fifo;
  wl_bipbuf_t tx_fifo;

  const wl_config_t *config;

  wl_sink_fn sink;
  void *sink_user_data;

  wl_tx_state_t tx_state;
  wl_tx_handle_t tx_handle;
  wl_tx_handle_t tx_next_handle;
  wl_io_token_t tx_token;
  wl_time_ms_t tx_start_ts;
  uint16_t tx_retries_left;
  uint16_t tx_retries_max;
  uint32_t tx_sequence;
  uint32_t tx_waiting_seq;
  uint8_t tx_inflight;
  uint8_t tx_waiting_ack;

  uint64_t session_id;
  uint32_t seq_recv;

  wl_event_t event;
  uint8_t has_event;
  uint8_t in_callback;
  uint8_t in_flight_reliable;

  wl_time_ms_t now_ms;

  uint8_t cobs_accum[WL_FRAME_MAX_COBS_LEN];
  size_t cobs_accum_len;
  uint8_t cobs_overflow;

  wl_span_t rx_payload;
  uint8_t rx_payload_storage[WL_FRAME_MAX_PAYLOAD];

  wl_span_t tx_payload;
  uint8_t tx_payload_storage[WL_FRAME_MAX_PAYLOAD];
} wl_ctx_t;

int wl_init(wl_ctx_t *ctx, const wl_config_t *config, uint8_t *rx_mem,
            size_t rx_mem_size, uint8_t *tx_mem, size_t tx_mem_size);
int wl_set_sink(wl_ctx_t *ctx, wl_sink_fn sink, void *user_data);

int wl_send_unreliable(wl_ctx_t *ctx, uint16_t cmd_id, const uint8_t *payload,
                       size_t payload_len);
int wl_send_reliable(wl_ctx_t *ctx, uint16_t cmd_id, const uint8_t *payload,
                     size_t payload_len, wl_tx_handle_t *out_handle);

int wl_tx_status(const wl_ctx_t *ctx, wl_tx_handle_t handle,
                 wl_tx_state_t *out_state);
int wl_tx_cancel(wl_ctx_t *ctx, wl_tx_handle_t handle);

int wl_feed_bytes(wl_ctx_t *ctx, const uint8_t *data, size_t len);
int wl_feed_unit(wl_ctx_t *ctx, const uint8_t *unit, size_t len);

int wl_poll(wl_ctx_t *ctx, wl_time_ms_t now_ms, wl_event_t *out_event);
void wl_event_release(wl_ctx_t *ctx, const wl_event_t *event);

int wl_tx_complete(wl_ctx_t *ctx, wl_io_token_t token, int io_result);
int wl_feed_recover_reset(wl_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // INCLUDE_WIRELINK_WIRELINK_H_
