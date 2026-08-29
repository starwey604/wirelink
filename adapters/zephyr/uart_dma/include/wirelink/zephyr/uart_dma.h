/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_ZEPHYR_UART_DMA_H_
#define WIRELINK_ZEPHYR_UART_DMA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>

#include "wirelink/wirelink.h"

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
  atomic_t running;
  atomic_t paused;
  atomic_t abort_pending;
  atomic_t recovery_barrier;
  atomic_t expected_disabled;
  /* Callback-to-service ownership handoff, one bit per slot. */
  atomic_t released_slots;
  atomic_t buffer_requests;
  atomic_t rx_ready_events;
  atomic_t published_bytes;
  atomic_t producer_cycles;
  atomic_t errors;
} wl_zephyr_uart_dma_t;

typedef struct {
  uint32_t buffer_requests;
  uint32_t rx_ready_events;
  uint32_t published_bytes;
  uint32_t producer_cycles;
  uint32_t errors;
  uint8_t running;
  uint8_t paused;
} wl_zephyr_uart_dma_stats_t;

/* Initialize and register the UART callback. Does not start reception. */
int wl_zephyr_uart_dma_init(wl_zephyr_uart_dma_t *adapter,
                            const wl_zephyr_uart_dma_config_t *config);

/* Start a direct-to-Wirelink DMA receive stream. */
int wl_zephyr_uart_dma_start(wl_zephyr_uart_dma_t *adapter);

/*
 * Call from the single consumer context after wl_poll(). It performs deferred
 * recovery and resumes RX after backpressure. A recovery barrier deliberately
 * consumes one service call so wl_poll() can discard the aborted COBS window.
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
