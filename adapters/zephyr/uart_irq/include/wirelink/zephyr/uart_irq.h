/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_UART_IRQ_H_
#define WIRELINK_ZEPHYR_UART_IRQ_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "wirelink/byte_fifo.h"
#include "wirelink/transport.h"

struct uart_driver_api;
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const struct device *uart;
  wl_span_t fifo_storage; /* Staging memory owned by this transport until close. */
  uint32_t drain_check_ms; /* Physical line-idle polling interval; 0 selects 1 ms. */
  /* Optional additional ISR-safe, nonblocking, latched notifier. Called once
   * per callback with publications, under the binding lock. Must not reenter
   * this adapter; context lives through stop. Notifications may be coalesced. */
  void (*notify)(void *user_data);
  void *notify_user_data;
  uint32_t baud_rate; /* 0 keeps the existing line configuration; otherwise 8N1. */
  bool wait_for_tx_idle; /* Default: complete at FIFO acceptance. */
  /* Coalesce wakes for incomplete long COBS frames. Wake on a delimiter,
   * 256 bytes or half the FIFO (whichever comes first), and on faults or TX
   * completion. Short prefixes retain the normal immediate notification. */
  bool disable_rx_batching; /* Opt out for immediate per-read notification. */
} wl_zephyr_uart_irq_config_t;

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
} wl_zephyr_uart_irq_stats_t;

typedef struct {
  wl_transport_t transport;
  wl_zephyr_uart_irq_config_t config;
  wl_byte_fifo_t fifo;
  const struct device *uart;
  const struct uart_driver_api *private_uart_api;
  wl_packet_port_t *port;
  wl_packet_stream_t *stream;
  const uint8_t *tx_data;
  size_t tx_length, tx_offset;
  wl_packet_token_t tx_token;
  struct k_sem wake;
  void (*notify)(void *user_data);
  void *notify_user_data;
  uint32_t drain_check_ms;
  uint32_t drain_due;
  atomic_t batch_active;
  uint32_t rx_unnotified;
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
} wl_zephyr_uart_irq_t;
/* Configure before binding a node. Keep hardware buffers and callback contexts
 * live through node close. */
wl_transport_t *wl_zephyr_uart_irq_transport(wl_zephyr_uart_irq_t *adapter,
    const wl_zephyr_uart_irq_config_t *config);

#define WL_ZEPHYR_UART_IRQ_DEFINE(name, uart_dev, fifo_bytes) \
  static struct {                                             \
    wl_zephyr_uart_irq_t adapter;                             \
    uint8_t fifo[fifo_bytes];                                 \
    const struct device *uart;                                \
  } name = {.uart = (uart_dev)}

static inline wl_transport_t *wl_zephyr_uart_irq_transport_default(
    wl_zephyr_uart_irq_t *adapter, const struct device *uart, uint8_t *fifo,
    size_t fifo_size, uint32_t baud_rate) {
  const wl_zephyr_uart_irq_config_t config = {
    .uart = uart, .fifo_storage = {fifo, fifo_size}, .baud_rate = baud_rate
  };
  return wl_zephyr_uart_irq_transport(adapter, &config);
}

#define wl_zephyr_uart_irq_open_static(name, baud_rate) \
  wl_zephyr_uart_irq_transport_default(&(name).adapter, (name).uart, (name).fifo, \
                                      sizeof((name).fifo), (baud_rate))

/* Check the physical drain and IRQ API without taking ownership of the UART. */
wl_err_t wl_zephyr_uart_irq_probe(const struct device *uart);
int wl_zephyr_uart_irq_service(wl_zephyr_uart_irq_t *adapter, uint32_t now_ms);
wl_io_readiness_t wl_zephyr_uart_irq_readiness(const void *adapter, uint32_t now_ms);
void wl_zephyr_uart_irq_stop(wl_zephyr_uart_irq_t *adapter);
/* Platform business deadlines can use sub-millisecond Zephyr timeouts. */
wl_err_t wl_zephyr_uart_irq_wait(wl_zephyr_uart_irq_t *adapter, k_timeout_t timeout);
void wl_zephyr_uart_irq_get_stats(const wl_zephyr_uart_irq_t *adapter,
                                  wl_zephyr_uart_irq_stats_t *out_stats);
wl_packet_submit_t wl_zephyr_uart_irq_submit(void *, const uint8_t *, size_t, wl_packet_token_t);
void wl_zephyr_uart_irq_notify(void *);

#ifdef __cplusplus
}
#endif
#endif
