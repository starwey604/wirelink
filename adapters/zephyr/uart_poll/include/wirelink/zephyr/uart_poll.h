/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_UART_POLL_H_
#define WIRELINK_ZEPHYR_UART_POLL_H_
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "wirelink/byte_fifo.h"
#include "wirelink/transport.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental polling transport. RX worker only stages bytes. Polling TX is
 * synchronous on the owner; a required platform idle query establishes physical
 * drain. A driver that cannot provide it is explicitly unsupported. */
typedef struct {
  const struct device *uart;
  wl_span_t fifo_storage; /* Staging memory owned by this transport until close. */
  k_thread_stack_t *stack;
  size_t stack_size;
  int priority;
  int (*tx_idle)(const struct device *);
  uint32_t drain_check_ms;
  void (*notify)(void *);
  void *notify_user;
} wl_zephyr_uart_poll_config_t;
typedef struct {
  wl_transport_t transport;
  wl_zephyr_uart_poll_config_t config;
  wl_byte_fifo_t fifo;
  wl_packet_port_t *port;
  wl_packet_stream_t *stream;
  struct k_thread worker;
  struct k_sem wake, stop_wake;
  atomic_t running, error_pending, errors;
  bool initialized, worker_created, error_api_available, tx_active;
  wl_packet_token_t tx_token;
  uint32_t drain_due;
} wl_zephyr_uart_poll_t;
/* Configure before binding a node. Keep hardware buffers and callback contexts
 * live through node close. */
wl_transport_t *wl_zephyr_uart_poll_transport(wl_zephyr_uart_poll_t *adapter,
    const wl_zephyr_uart_poll_config_t *config);

wl_packet_submit_t wl_zephyr_uart_poll_submit(void *, const uint8_t *, size_t, wl_packet_token_t);
void wl_zephyr_uart_poll_notify(void *);
int wl_zephyr_uart_poll_service(wl_zephyr_uart_poll_t *, uint32_t);
wl_io_readiness_t wl_zephyr_uart_poll_readiness(const wl_zephyr_uart_poll_t *, uint32_t);
wl_err_t wl_zephyr_uart_poll_wait(wl_zephyr_uart_poll_t *, k_timeout_t);
void wl_zephyr_uart_poll_request_stop(wl_zephyr_uart_poll_t *);
/* Owner only; joins RX then waits for real physical idle. WOULD_BLOCK retains
 * the adapter/port/stream/stack lifetime. Never call from worker/notify/sink. */
int wl_zephyr_uart_poll_stop(wl_zephyr_uart_poll_t *);
#ifdef __cplusplus
}
#endif
#endif
