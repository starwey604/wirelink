/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/kernel_uart_poll.h"
#include <zephyr/drivers/uart.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
enum { READY, RUNNING, STOPPED };
void wl_kernel_uart_poll_notify(void *user) {
  wl_kernel_uart_poll_t *a = user;
  k_sem_give(&a->wake);
  if (a->config.notify)
    a->config.notify(a->config.notify_user);
}
static void error(wl_kernel_uart_poll_t *a) {
  atomic_inc(&a->errors);
  atomic_set(&a->error_pending, 1);
  wl_byte_fifo_note_gap(a->config.fifo);
}
static void receive(void *user, void *unused1, void *unused2) {
  wl_kernel_uart_poll_t *a = user;
  (void)unused1;
  (void)unused2;
  while (atomic_get(&a->running) == RUNNING) {
    bool published = false;
    if (a->error_api_available && uart_err_check(a->config.uart) > 0) {
      error(a);
      published = true;
    }
    wl_span_t span;
    if (wl_byte_fifo_reserve(a->config.fifo, &span) == WL_OK) {
      size_t count = 0U, limit = MIN(span.length, 64U);
      while (count < limit && atomic_get(&a->running) == RUNNING) {
        int result = uart_poll_in(a->config.uart, span.data + count);
        if (result != 0) {
          if (result != -1) {
            error(a);
            published = true;
          }
          break;
        }
        ++count;
      }
      if (wl_byte_fifo_commit(a->config.fifo, count) != WL_OK) {
        error(a);
        published = true;
      }
      if (count != 0U)
        published = true;
    }
    if (published)
      wl_kernel_uart_poll_notify(a);
    // One tick blocks even when a lower-priority owner is runnable.
    if (atomic_get(&a->running) == RUNNING)
      (void)k_sem_take(&a->stop_wake, K_TICKS(1));
  }
}
int wl_kernel_uart_poll_init(wl_kernel_uart_poll_t *a, const wl_kernel_uart_poll_config_t *c) {
  if (a == NULL || c == NULL || c->uart == NULL || !device_is_ready(c->uart) || c->port == NULL ||
      c->fifo == NULL || c->stream == NULL || c->stack == NULL || c->stack_size == 0U ||
      c->tx_idle == NULL || c->drain_check_ms > INT32_MAX || c->priority < K_HIGHEST_THREAD_PRIO ||
      c->priority > K_LOWEST_APPLICATION_THREAD_PRIO)
    return WL_ERR_INVALID_ARG;
  const struct uart_driver_api *api = DEVICE_API_GET(uart, c->uart);
  if (api == NULL || api->poll_in == NULL || api->poll_out == NULL)
    return WL_ERR_NOT_SUPPORTED;
  const wl_kernel_uart_poll_config_t saved = *c;
  memset(a, 0, sizeof(*a));
  a->config = saved;
  k_sem_init(&a->wake, 0, 1);
  k_sem_init(&a->stop_wake, 0, 1);
  a->error_api_available = api->err_check != NULL;
  a->initialized = true;
  return WL_OK;
}
int wl_kernel_uart_poll_start(wl_kernel_uart_poll_t *a) {
  if (a == NULL || !a->initialized)
    return WL_ERR_INVALID_ARG;
  if (!atomic_cas(&a->running, READY, RUNNING))
    return WL_ERR_INVALID_STATE;
  a->worker_created = true;
  k_thread_create(&a->worker, a->config.stack, a->config.stack_size, receive, a, NULL, NULL,
                  a->config.priority, 0, K_NO_WAIT);
  k_thread_name_set(&a->worker, "wl_packet_uart_rx");
  return WL_OK;
}
wl_packet_submit_t wl_kernel_uart_poll_submit(void *user, const uint8_t *bytes, size_t length,
                                              wl_packet_token_t token) {
  wl_kernel_uart_poll_t *a = user;
  if (a == NULL || bytes == NULL || length == 0U || atomic_get(&a->running) != RUNNING)
    return WL_PACKET_FAILED;
  if (a->tx_active)
    return WL_PACKET_BUSY;
  for (size_t i = 0U; i < length; ++i) {
    if (atomic_get(&a->running) != RUNNING) {
      error(a);
      return WL_PACKET_FAILED;
    }
    uart_poll_out(a->config.uart, bytes[i]);
  }
  a->tx_active = true;
  a->tx_token = token;
  a->drain_due = k_uptime_get_32();
  (void)wl_packet_tx_publish(a->config.port, token, WL_PACKET_CONSUMED);
  return WL_PACKET_STARTED;
}
static int drain(wl_kernel_uart_poll_t *a, uint32_t now) {
  if (!a->tx_active || (int32_t)(now - a->drain_due) < 0)
    return WL_OK;
  int idle = a->config.tx_idle(a->config.uart);
  if (idle > 0) {
    a->tx_active = false;
    return wl_packet_tx_publish(a->config.port, a->tx_token, WL_PACKET_DRAINED);
  }
  a->drain_due = now + (a->config.drain_check_ms ? a->config.drain_check_ms : 1U);
  return idle < 0 ? WL_ERR_IO : WL_OK;
}
int wl_kernel_uart_poll_service(wl_kernel_uart_poll_t *a, uint32_t now) {
  if (a == NULL || !a->initialized)
    return WL_ERR_INVALID_ARG;
  (void)k_sem_take(&a->wake, K_NO_WAIT);
  int result = atomic_set(&a->error_pending, 0) ? WL_ERR_IO : WL_OK;
  int d = drain(a, now);
  int input = wl_byte_fifo_to_cobs(a->config.fifo, a->config.stream, 16U);
  if (result == WL_OK && d != WL_OK)
    result = d;
  if (result == WL_OK && input != WL_ERR_WOULD_BLOCK)
    result = input;
  return result;
}
wl_io_readiness_t wl_kernel_uart_poll_readiness(const wl_kernel_uart_poll_t *a, uint32_t now) {
  wl_io_readiness_t r = wl_io_readiness_idle();
  r.runnable =
      atomic_get(&a->error_pending) || wl_byte_fifo_cobs_ready(a->config.fifo, a->config.stream);
  if (a->tx_active)
    wl_io_readiness_merge(
        &r, wl_io_readiness_at((int32_t)(now - a->drain_due) >= 0 ? 0U : a->drain_due - now));
  return r;
}
wl_err_t wl_kernel_uart_poll_wait(wl_kernel_uart_poll_t *a, k_timeout_t timeout) {
  if (a == NULL || atomic_get(&a->running) != RUNNING)
    return WL_ERR_CANCELLED;
  int result = k_sem_take(&a->wake, timeout);
  if (atomic_get(&a->running) != RUNNING)
    return WL_ERR_CANCELLED;
  return result == 0 ? WL_OK : WL_ERR_NO_DATA;
}
void wl_kernel_uart_poll_request_stop(wl_kernel_uart_poll_t *a) {
  if (a == NULL || !a->initialized)
    return;
  atomic_set(&a->running, STOPPED);
  k_sem_give(&a->stop_wake);
  k_sem_give(&a->wake);
}
int wl_kernel_uart_poll_stop(wl_kernel_uart_poll_t *a) {
  if (a == NULL || !a->initialized)
    return WL_ERR_INVALID_ARG;
  __ASSERT(k_current_get() != &a->worker, "worker cannot join itself");
  wl_kernel_uart_poll_request_stop(a);
  wl_packet_stop(a->config.port);
  if (a->worker_created) {
    (void)k_thread_join(&a->worker, K_FOREVER);
    a->worker_created = false;
  }
  if (a->tx_active) {
    a->drain_due = k_uptime_get_32();
    int result = drain(a, a->drain_due);
    if (result != WL_OK)
      return result;
    if (a->tx_active)
      return WL_ERR_WOULD_BLOCK;
  }
  (void)wl_packet_stream_abort_rx(a->config.stream);
  a->initialized = false;
  a->config.notify = NULL;
  return WL_OK;
}
