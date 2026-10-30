/* SPDX-License-Identifier: Apache-2.0 */

#ifndef INCLUDE_WIRELINK_PORT_H_
#define INCLUDE_WIRELINK_PORT_H_

#include "wirelink/link.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t wl_sink_result_t;
/* SENT completes source-memory access synchronously. If physical drain was
 * configured, SENT also guarantees that observable boundary is complete.
 * Otherwise return STARTED and publish consumed/drained separately on owner. */
enum { WL_SINK_SENT = 0, WL_SINK_STARTED, WL_SINK_BUSY, WL_SINK_FAILED };

typedef wl_sink_result_t (*wl_sink_fn)(void *user_data, wl_io_token_t token,
                                      const uint8_t *data, size_t len);

#define WL_RX_DMA_MAX_CLAIMS 1U
#define WL_RX_UNIT_QUEUE_MAX_SLOTS 8U

typedef struct wl_rx_dma_claim {
  wl_span_t span;
  uint32_t token;
} wl_rx_dma_claim_t;

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

wl_err_t wl_set_sink(wl_ctx_t *ctx, wl_sink_fn sink, void *user_data);

/* Producer-side ingress. Parsing and callbacks remain on the link owner. */
wl_err_t wl_feed_bytes(wl_ctx_t *ctx, const uint8_t *data, size_t len,
                       size_t *out_accepted);
wl_err_t wl_rx_reserve(wl_ctx_t *ctx, wl_span_t *out_span);
wl_err_t wl_rx_commit(wl_ctx_t *ctx, size_t len);
wl_err_t wl_rx_dma_claim(wl_ctx_t *ctx, size_t maximum_length,
                         wl_rx_dma_claim_t *out_claim);
wl_err_t wl_rx_dma_publish(wl_ctx_t *ctx, const wl_rx_dma_claim_t *claim,
                           size_t offset, size_t length);
wl_err_t wl_rx_dma_finish(wl_ctx_t *ctx, const wl_rx_dma_claim_t *claim);
wl_err_t wl_rx_dma_abort(wl_ctx_t *ctx);
wl_err_t wl_rx_unit_queue_init(wl_ctx_t *ctx,
                               const wl_rx_unit_queue_config_t *config);
/* Setup-time, read-only queue description. Does not claim, reset or release
 * slots. Storage remains owned by the application until the link is closed. */
wl_err_t wl_rx_unit_queue_get_config(const wl_ctx_t *ctx,
                                     wl_rx_unit_queue_config_t *out_config);
/* Nonmutating resource query for the single producer/owner. A packet link
 * without an optional queue accepts one unit after its RX lease is returned. */
uint8_t wl_rx_can_accept_unit(const wl_ctx_t *ctx);
wl_err_t wl_rx_unit_claim(wl_ctx_t *ctx, size_t maximum_length,
                          wl_rx_unit_claim_t *out_claim);
wl_err_t wl_rx_unit_commit(wl_ctx_t *ctx, const wl_rx_unit_claim_t *claim,
                           size_t length);
wl_err_t wl_rx_unit_abort(wl_ctx_t *ctx, const wl_rx_unit_claim_t *claim);
void wl_rx_note_overflow(wl_ctx_t *ctx);
wl_err_t wl_feed_unit(wl_ctx_t *ctx, const uint8_t *unit, size_t len);

/* Consumer-side completion and recovery hooks used by platform ports. */
/* consumed ends source-memory access, not physical idle, ACK or RPC success.
 * Enable a distinct drained report before attaching a physical-idle adapter.
 * A drained report starts the ACK timer using this owner pass's clock snapshot.
 * Owner-only; producers latch reports and wake the owner, never call here. */
wl_err_t wl_set_tx_drain_required(wl_ctx_t *ctx, uint8_t required);
wl_err_t wl_tx_consumed(wl_ctx_t *ctx, wl_io_token_t token, int io_result);
wl_err_t wl_tx_drained(wl_ctx_t *ctx, wl_io_token_t token, int io_result);
/* Publish transport writable after a BUSY response, then wake the owner. */
wl_err_t wl_tx_writable(wl_ctx_t *ctx);
uint8_t wl_tx_can_take(const wl_ctx_t *ctx, wl_tx_handle_t handle);
/* Owner-only resource release generation. Changes on consumed/drained,
 * writable, terminal take, claim abort and RX lease return; never a wake by itself. */
uint32_t wl_link_resource_generation(const wl_ctx_t *ctx);
uint8_t wl_tx_can_submit(const wl_ctx_t *ctx, wl_delivery_t delivery);
uint8_t wl_rx_has_space(const wl_ctx_t *ctx);
wl_err_t wl_feed_recover_reset(wl_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_WIRELINK_PORT_H_ */
