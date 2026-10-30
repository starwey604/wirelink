/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_KERNEL_ZEPHYR_UART_IRQ_H_
#define WIRELINK_KERNEL_ZEPHYR_UART_IRQ_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "wirelink/byte_fifo.h"
#include "wirelink/io_readiness.h"
struct uart_driver_api;
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const struct device *uart;
  wl_packet_port_t *port;
  wl_byte_fifo_t *fifo;
  wl_packet_stream_t *stream;
  /* Physical UART only. TX-empty IRQ is disabled at memory consumed; line
   * idle is queried once per deadline. CDC cannot provide this boundary. */
  uint32_t drain_check_ms; /* 0 selects 1 ms; physical drain is always required. */
  /* Optional additional ISR-safe, nonblocking, latched notifier. Called once
   * per callback with publications, under the binding lock. Must not reenter
   * this adapter; context lives through stop. Notifications may be coalesced. */
  void (*notify)(void *user_data);
  void *notify_user_data;
} wl_kernel_uart_irq_config_t;

typedef struct {
  uint32_t rx_irqs, rx_bytes, rx_pauses;
  uint32_t tx_irqs, tx_bytes, tx_consumed, tx_drained, drain_queries, errors;
#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_NOTIFICATION_DIAGNOSTICS)
  uint32_t publications, callback_gives, service_acks, waits, wait_ready;
#endif
#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_TIMING)
  uint32_t callback_irqs, callback_ticks;
#endif
  bool started, rx_paused, tx_active;
} wl_kernel_uart_irq_stats_t;

typedef struct {
  const struct device *uart;
  const struct uart_driver_api *private_uart_api;
  wl_packet_port_t *port;
  wl_byte_fifo_t *fifo;
  wl_packet_stream_t *stream;
  const uint8_t *tx_data;
  size_t tx_length, tx_offset;
  wl_packet_token_t tx_token;
  struct k_sem wake;
  void (*notify)(void *user_data);
  void *notify_user_data;
  uint32_t drain_check_ms;
  uint32_t drain_due;
  uint8_t binding, wait_for_tx_idle, drain_pending;
  atomic_t flags, tx_completion, error_pending;
  atomic_t rx_irqs, rx_bytes, rx_pauses;
  atomic_t tx_irqs, tx_bytes, tx_consumed, tx_drained, drain_queries, errors;
#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_NOTIFICATION_DIAGNOSTICS)
  atomic_t publications, callback_gives, service_acks, waits, wait_ready;
#endif
#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_TIMING)
  atomic_t callback_irqs, callback_ticks;
#endif
} wl_kernel_uart_irq_t;

/* Explicit packet/byte-FIFO assembly; caller merges readiness and deadlines.
 * IRQ stop synchronizes callbacks through the generation binding lock. */
int wl_kernel_uart_irq_init(wl_kernel_uart_irq_t *adapter,
                            const wl_kernel_uart_irq_config_t *config);
int wl_kernel_uart_irq_start(wl_kernel_uart_irq_t *adapter);
int wl_kernel_uart_irq_service(wl_kernel_uart_irq_t *adapter, uint32_t now_ms);
wl_io_readiness_t wl_kernel_uart_irq_readiness(const void *adapter, uint32_t now_ms);
void wl_kernel_uart_irq_stop(wl_kernel_uart_irq_t *adapter);
/* Platform business deadlines can use sub-millisecond Zephyr timeouts. */
wl_err_t wl_kernel_uart_irq_wait(wl_kernel_uart_irq_t *adapter, k_timeout_t timeout);
void wl_kernel_uart_irq_get_stats(const wl_kernel_uart_irq_t *adapter,
                                  wl_kernel_uart_irq_stats_t *out_stats);
wl_packet_submit_t wl_kernel_uart_irq_submit(void *, const uint8_t *, size_t, wl_packet_token_t);
void wl_kernel_uart_irq_notify(void *);

#ifdef __cplusplus
}
#endif
#endif
