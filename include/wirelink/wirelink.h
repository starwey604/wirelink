/* SPDX-License-Identifier: Apache-2.0 */

#ifndef INCLUDE_WIRELINK_WIRELINK_H_
#define INCLUDE_WIRELINK_WIRELINK_H_

#include <stddef.h>
#include <stdint.h>

#include "wirelink/frame.h"
#include "wirelink/span.h"
#include "wirelink/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t wl_time_ms_t;
typedef uint32_t wl_tx_handle_t;
typedef uint32_t wl_io_token_t;

/*
 * Allocation-free opaque context storage. Applications may allocate this as
 * a static object or on the stack, but must not inspect its private bytes.
 * The size and alignment are stable for the Wirelink v1 ABI.
 */
#define WL_CONTEXT_STORAGE_SIZE 896U
typedef union wl_ctx {
  max_align_t align;
  uint8_t private_bytes[WL_CONTEXT_STORAGE_SIZE];
} wl_ctx_t;

typedef int32_t wl_tx_state_t;
enum {
  WL_TX_STATE_IDLE = 0,
  WL_TX_STATE_SENDING,
  WL_TX_STATE_WAITING_ACK,
  WL_TX_STATE_SUCCESS,
  WL_TX_STATE_FAILED,
  WL_TX_STATE_CANCELLED,
};

typedef int32_t wl_tx_wait_reason_t;
enum {
  WL_TX_WAIT_NONE = 0,
  WL_TX_WAIT_ACK,
};

typedef int32_t wl_event_type_t;
enum {
  WL_EVT_NONE = 0,
  WL_EVT_UNRELIABLE_RX,
  WL_EVT_RELIABLE_RX,
  WL_EVT_TX_SUCCESS,
  WL_EVT_TX_TIMEOUT,
  WL_EVT_TX_FAILED,
};

typedef struct {
  wl_event_type_t type;
  uint16_t message_id;
  const uint8_t *payload;
  size_t payload_len;
  wl_tx_handle_t handle;
  int io_result;
  uint32_t lease;
} wl_event_t;

typedef struct {
  wl_tx_state_t state;
  int result;
  uint16_t retries_used;
} wl_tx_result_t;

/*
 * Relative scheduling information for the single Wirelink consumer.
 * work_pending is either zero or one.  next_deadline_ms is measured from the
 * now_ms supplied to wl_poll_get_hint(), or WL_POLL_NO_DEADLINE_MS when the
 * protocol has no active timed deadline.
 */
#define WL_POLL_NO_DEADLINE_MS UINT32_MAX
typedef struct {
  uint32_t work_pending;
  uint32_t next_deadline_ms;
} wl_poll_hint_t;

typedef int32_t wl_sink_result_t;
enum {
  WL_SINK_SENT = 0,
  WL_SINK_STARTED,
  WL_SINK_BUSY,
  WL_SINK_FAILED,
};

typedef wl_sink_result_t (*wl_sink_fn)(void *user_data, wl_io_token_t token,
                                       const uint8_t *data, size_t len);

typedef struct {
  uint16_t max_payload_len;
  wl_envelope_type_t envelope;
  wl_integrity_t integrity;
  uint64_t session_id;
  uint16_t max_retries;
  uint32_t ack_timeout_ms;
  size_t max_transmission_unit;
} wl_config_t;

/* All long-lived protocol bytes are supplied by the application. */
typedef struct {
  uint8_t *tx_payload;
  size_t tx_payload_size;
  uint8_t *tx_unit;
  size_t tx_unit_size;
  uint8_t *control_unit;
  size_t control_unit_size;
  uint8_t *rx_fifo;
  size_t rx_fifo_size;
  uint8_t *rx_fallback;
  size_t rx_fallback_size;
} wl_storage_t;

typedef struct {
  size_t tx_payload_size;
  size_t tx_unit_size;
  size_t control_unit_size;
  size_t rx_fifo_size;
  size_t rx_fallback_size;
} wl_storage_requirements_t;

typedef struct {
  uint32_t malformed;
  uint32_t bad_integrity;
  uint32_t overflow;
  uint32_t duplicate;
  uint32_t unsupported;
} wl_rx_counters_t;

/* At most two UART/DMA buffers may be owned by a direct RX producer. */
#define WL_RX_DMA_MAX_CLAIMS 2U
#define WL_RX_UNIT_QUEUE_MAX_SLOTS 8U

/*
 * A direct-DMA claim owns a contiguous range in the RX ring. Its span is
 * writable only by the single producer until wl_rx_dma_finish() or
 * wl_rx_dma_abort(). Applications must treat token as opaque.
 */
typedef struct wl_rx_dma_claim {
  wl_span_t span;
  uint32_t token;
} wl_rx_dma_claim_t;

/*
 * Packet transports may let their single producer write directly into a
 * fixed-slot SPSC queue. A committed unit is parsed by wl_poll(); an RX event
 * borrows its slot until wl_event_release().
 */
typedef struct wl_rx_unit_queue_config {
  uint8_t *storage;
  size_t storage_size;
  size_t unit_size;
  uint8_t slot_count;
} wl_rx_unit_queue_config_t;

typedef struct wl_rx_unit_claim {
  wl_span_t span;
  uint32_t token;
} wl_rx_unit_claim_t;

typedef int32_t wl_delivery_t;
enum {
  WL_DELIVERY_UNRELIABLE = 0,
  WL_DELIVERY_RELIABLE = 1,
};

/* Native-packet TX payload written directly into the final unit buffer. */
typedef struct wl_tx_payload_claim {
  wl_span_t span;
  uint32_t token;
} wl_tx_payload_claim_t;

int wl_config_requirements(const wl_config_t *config,
                           wl_storage_requirements_t *out_requirements);
/* Config and the storage descriptor are copied; pointed-to buffers are not. */
int wl_init(wl_ctx_t *ctx, const wl_config_t *config,
            const wl_storage_t *storage);
int wl_get_config(const wl_ctx_t *ctx, wl_config_t *out_config);
int wl_set_sink(wl_ctx_t *ctx, wl_sink_fn sink, void *user_data);
int wl_rx_get_counters(const wl_ctx_t *ctx, wl_rx_counters_t *out_counters);

int wl_send_unreliable(wl_ctx_t *ctx, uint16_t message_id, const uint8_t *payload,
                       size_t payload_len);
int wl_send_reliable(wl_ctx_t *ctx, uint16_t message_id, const uint8_t *payload,
                     size_t payload_len, wl_tx_handle_t *out_handle);
int wl_tx_payload_claim(wl_ctx_t *ctx, uint16_t message_id,
                        wl_delivery_t delivery,
                        wl_tx_payload_claim_t *out_claim);
int wl_tx_payload_commit(wl_ctx_t *ctx,
                         const wl_tx_payload_claim_t *claim,
                         size_t payload_len, wl_tx_handle_t *out_handle);
int wl_tx_payload_abort(wl_ctx_t *ctx,
                        const wl_tx_payload_claim_t *claim);

int wl_tx_status(const wl_ctx_t *ctx, wl_tx_handle_t handle,
                 wl_tx_state_t *out_state);
int wl_tx_take(wl_ctx_t *ctx, wl_tx_handle_t handle,
               wl_tx_result_t *out_result);
int wl_tx_cancel(wl_ctx_t *ctx, wl_tx_handle_t handle);

/* Single-producer entry points: no parsing, callbacks, or ACK work is done. */
int wl_feed_bytes(wl_ctx_t *ctx, const uint8_t *data, size_t len,
                  size_t *out_accepted);
int wl_rx_reserve(wl_ctx_t *ctx, wl_span_t *out_span);
int wl_rx_commit(wl_ctx_t *ctx, size_t len);
/*
 * Direct-DMA producer lifecycle. Claims are published and finished strictly
 * in claim order. publish() accepts only the next un-published prefix.
 * finish() may close a partially published claim only when it is the last
 * outstanding claim; its un-published tail is then returned to the ring.
 * A producer with a successor claim must finish its preceding claim in full,
 * so the ring never has a logical hole.
 * abort() is valid only after the platform has stopped DMA access to every
 * claim; it discards un-published space and forces COBS resynchronization.
 */
int wl_rx_dma_claim(wl_ctx_t *ctx, size_t maximum_length,
                    wl_rx_dma_claim_t *out_claim);
int wl_rx_dma_publish(wl_ctx_t *ctx, const wl_rx_dma_claim_t *claim,
                      size_t offset, size_t length);
int wl_rx_dma_finish(wl_ctx_t *ctx, const wl_rx_dma_claim_t *claim);
int wl_rx_dma_abort(wl_ctx_t *ctx);
int wl_rx_unit_queue_init(wl_ctx_t *ctx,
                          const wl_rx_unit_queue_config_t *config);
int wl_rx_unit_claim(wl_ctx_t *ctx, size_t maximum_length,
                     wl_rx_unit_claim_t *out_claim);
int wl_rx_unit_commit(wl_ctx_t *ctx, const wl_rx_unit_claim_t *claim,
                      size_t length);
int wl_rx_unit_abort(wl_ctx_t *ctx, const wl_rx_unit_claim_t *claim);
void wl_rx_note_overflow(wl_ctx_t *ctx);
int wl_feed_unit(wl_ctx_t *ctx, const uint8_t *unit, size_t len);

int wl_poll(wl_ctx_t *ctx, wl_time_ms_t now_ms, wl_event_t *out_event);
int wl_poll_get_hint(const wl_ctx_t *ctx, wl_time_ms_t now_ms,
                     wl_poll_hint_t *out_hint);
void wl_event_release(wl_ctx_t *ctx, const wl_event_t *event);

int wl_tx_complete(wl_ctx_t *ctx, wl_io_token_t token, int io_result);
int wl_feed_recover_reset(wl_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_WIRELINK_WIRELINK_H_ */
