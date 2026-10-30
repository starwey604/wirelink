/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_UART_POLL_H_
#define WIRELINK_ZEPHYR_UART_POLL_H_
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "wirelink/endpoint.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental physical UART, no hardware flow control or drained completion.
 * Caller supplies stable adapter + stack storage through stop/join. One owner,
 * one RX producer; do not enable another producer on the same UART. */
typedef struct {
  const struct device *uart;
  k_thread_stack_t *stack;
  size_t stack_size;
  int priority;
  bool wait_for_tx_idle; /* true is explicitly rejected. */
  void (*notify)(void *);
  void *notify_user_data;
} wl_zephyr_uart_poll_config_t;
typedef struct {
  uint32_t rx_bytes, rx_pauses, tx_bytes, tx_consumed, errors;
  bool started, tx_active, error_api_available;
} wl_zephyr_uart_poll_stats_t;
typedef struct {
  const struct device *uart;
  wl_ctx_t *link;
  struct k_thread worker;
  k_thread_stack_t *stack;
  size_t stack_size;
  int priority;
  struct k_sem wake, stop_wake;
  void (*notify)(void *);
  void *notify_user_data;
  atomic_t running, tx_active, rx_bytes, rx_pauses, tx_bytes, tx_consumed, errors;
  atomic_t error_pending;
  atomic_t initialized;
  bool worker_created, error_api_available;
} wl_zephyr_uart_poll_t;
#define WL_ZEPHYR_UART_POLL_BATCH 64U
#define WL_ZEPHYR_UART_POLL_TICKS 1U
int wl_zephyr_uart_poll_attach(wl_zephyr_uart_poll_t *, wl_endpoint_t *,
    const wl_zephyr_uart_poll_config_t *);
int wl_zephyr_uart_poll_start(wl_zephyr_uart_poll_t *);
/* Cross-thread request only; storage and endpoint remain alive until owner
 * safe-point stop has joined the worker. Does not close the protocol. */
void wl_zephyr_uart_poll_request_stop(wl_zephyr_uart_poll_t *);
/* Owner safe point only, never from RX worker or inside step/sink/notify.
 * Depends on uart_poll_out eventually returning; cannot abort a stuck driver. */
void wl_zephyr_uart_poll_stop(wl_zephyr_uart_poll_t *);
wl_err_t wl_zephyr_uart_poll_wait(wl_zephyr_uart_poll_t *, k_timeout_t);
void wl_zephyr_uart_poll_get_stats(const wl_zephyr_uart_poll_t *, wl_zephyr_uart_poll_stats_t *);
#ifdef __cplusplus
}
#endif
#endif
