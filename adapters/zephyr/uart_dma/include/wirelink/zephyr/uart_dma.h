/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_ZEPHYR_UART_DMA_H_
#define WIRELINK_ZEPHYR_UART_DMA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>

#include "wirelink/port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wl_zephyr_uart_dma_cache_fn)(void *user_data, uint8_t *data,
                                            size_t length);
typedef uint32_t (*wl_zephyr_uart_dma_cycle_count_fn)(void *user_data);

typedef struct {
  const struct device *uart;
  wl_ctx_t *link;
  size_t maximum_chunk;
  int32_t timeout_us;
  int32_t tx_timeout_us;
  /* Work around DMA drivers whose TX_DONE precedes physical line idle. */
  bool wait_for_tx_idle;
  void *cache_user_data;
  wl_zephyr_uart_dma_cache_fn prepare_for_dma;
  wl_zephyr_uart_dma_cache_fn complete_from_dma;
  /* Optional ISR-safe cycle counter used only for ingress instrumentation. */
  void *cycle_counter_user_data;
  wl_zephyr_uart_dma_cycle_count_fn cycle_counter;
} wl_zephyr_uart_dma_config_t;

typedef struct {
  wl_rx_dma_claim_t claim;
  size_t received;
  size_t published;
} wl_zephyr_uart_dma_slot_t;

typedef struct {
  wl_zephyr_uart_dma_config_t config;
  wl_zephyr_uart_dma_slot_t slots[WL_RX_DMA_MAX_CLAIMS];
  wl_io_token_t tx_token;
  const uint8_t *tx_data;
  size_t tx_length;
  atomic_t started;
  atomic_t stopping;
  atomic_t running;
  atomic_t paused;
  atomic_t abort_pending;
  atomic_t recovery_barrier;
  atomic_t expected_disabled;
  atomic_t tx_active;
  atomic_t tx_completion;
  /* Callback-to-service ownership handoff, one bit per slot. */
  atomic_t released_slots;
  atomic_t buffer_requests;
  atomic_t rx_ready_events;
  atomic_t published_bytes;
  atomic_t producer_cycles;
  atomic_t tx_submissions;
  atomic_t tx_done_events;
  atomic_t tx_aborted_events;
  atomic_t tx_busy;
  atomic_t errors;
} wl_zephyr_uart_dma_t;

typedef struct {
  uint32_t buffer_requests;
  uint32_t rx_ready_events;
  uint32_t published_bytes;
  uint32_t producer_cycles;
  uint32_t tx_submissions;
  uint32_t tx_done_events;
  uint32_t tx_aborted_events;
  uint32_t tx_busy;
  uint32_t errors;
  uint8_t started;
  uint8_t stopping;
  uint8_t running;
  uint8_t paused;
  uint8_t tx_active;
} wl_zephyr_uart_dma_stats_t;

/*
 * Initialize the full-duplex adapter, register the UART's sole async callback,
 * and bind the Wirelink TX sink. The adapter must outlive the link context.
 * Does not start reception.
 */
int wl_zephyr_uart_dma_init(wl_zephyr_uart_dma_t *adapter,
                            const wl_zephyr_uart_dma_config_t *config);

/* Start a direct-to-Wirelink DMA receive stream. */
int wl_zephyr_uart_dma_start(wl_zephyr_uart_dma_t *adapter);

/*
 * Begin an asynchronous stop. New sink submissions fail immediately. The
 * driver may still own RX buffers or an active TX until callbacks hand them
 * back; keep calling service() until stats report started=0 and stopping=0.
 */
int wl_zephyr_uart_dma_stop(wl_zephyr_uart_dma_t *adapter);

/*
 * Call from the single consumer context after wl_poll(). It delivers deferred
 * TX completion to the core, performs RX recovery, completes stop requests,
 * and resumes RX after backpressure. A recovery barrier deliberately consumes
 * one service call so wl_poll() can discard the aborted COBS window.
 * WL_ERR_WOULD_BLOCK is transient: call service again from a later main-loop
 * iteration after the UART callback has handed buffer ownership back.
 */
int wl_zephyr_uart_dma_service(wl_zephyr_uart_dma_t *adapter);

/* Clear benchmark/telemetry counters without changing RX ownership. */
void wl_zephyr_uart_dma_reset_stats(wl_zephyr_uart_dma_t *adapter);
void wl_zephyr_uart_dma_get_stats(const wl_zephyr_uart_dma_t *adapter,
                                  wl_zephyr_uart_dma_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* WIRELINK_ZEPHYR_UART_DMA_H_ */
