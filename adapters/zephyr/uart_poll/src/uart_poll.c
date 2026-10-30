/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/uart_poll.h"
#include "wirelink/zephyr/detail/timeout.h"
#include "wirelink/port.h"
#include <zephyr/drivers/uart.h>
#include <errno.h>
#include <string.h>

enum { READY, RUNNING, STOPPED };

static void wake_owner(wl_zephyr_uart_poll_t *a) {
  k_sem_give(&a->wake);
  if (a->notify != NULL) a->notify(a->notify_user_data);
}
static void note_error(wl_zephyr_uart_poll_t *a) {
  atomic_inc(&a->errors);
  atomic_set(&a->error_pending, 1);
  wl_rx_note_overflow(a->link);
}
static void receive(void *context, void *unused1, void *unused2) {
  wl_zephyr_uart_poll_t *a = context;
  (void)unused1; (void)unused2;
  while (atomic_get(&a->running) == RUNNING) {
    wl_span_t span;
    bool published = false;
    int error = a->error_api_available ? uart_err_check(a->uart) : 0;
    if (error > 0) { note_error(a); published = true; }
    int result = wl_rx_reserve(a->link, &span);
    if (result == WL_OK && span.length != 0U) {
      size_t count = 0, limit = MIN(span.length, WL_ZEPHYR_UART_POLL_BATCH);
      while (count < limit && atomic_get(&a->running) == RUNNING) {
        result = uart_poll_in(a->uart, span.data + count);
        if (result != 0) {
          if (result != -1) { note_error(a); published = true; }
          break;
        }
        ++count;
      }
      if (wl_rx_commit(a->link, count) != WL_OK) { note_error(a); published = true; }
      if (count != 0U) { atomic_add(&a->rx_bytes, count); published = true; }
    } else {
      if (result == WL_OK) (void)wl_rx_commit(a->link, 0);
      if (result == WL_OK || result == WL_ERR_NO_SPACE || result == WL_ERR_WOULD_BLOCK) {
        /* Backpressure is not evidence of a physical FIFO overflow. */
        atomic_inc(&a->rx_pauses);
      } else { note_error(a); published = true; }
    }
    if (published) wake_owner(a); /* Publish partial batches before waking. */
    /* Really block even under continuous input: yield cannot run a lower
     * priority owner. A stop request releases this wait immediately. */
    if (atomic_get(&a->running) == RUNNING)
      (void)k_sem_take(&a->stop_wake, K_TICKS(WL_ZEPHYR_UART_POLL_TICKS));
  }
}
static wl_sink_result_t sink(void *context, wl_io_token_t token,
    const uint8_t *bytes, size_t length) {
  wl_zephyr_uart_poll_t *a = context;
  (void)token;
  if (atomic_get(&a->running) != RUNNING || bytes == NULL || length == 0) return WL_SINK_FAILED;
  atomic_set(&a->tx_active, 1);
  size_t sent = 0;
  while (sent < length && atomic_get(&a->running) == RUNNING) uart_poll_out(a->uart, bytes[sent++]);
  atomic_add(&a->tx_bytes, sent);
  atomic_clear(&a->tx_active);
  if (sent != length) { atomic_inc(&a->errors); return WL_SINK_FAILED; }
  atomic_inc(&a->tx_consumed);
  return WL_SINK_SENT; /* No retained source, not physical drained. */
}
wl_err_t wl_zephyr_uart_poll_wait(wl_zephyr_uart_poll_t *a, k_timeout_t timeout) {
  if (a == NULL) return WL_ERR_INVALID_ARG;
  if (atomic_get(&a->running) != RUNNING) return WL_ERR_CANCELLED;
  int result = k_sem_take(&a->wake, timeout);
  if (atomic_get(&a->running) != RUNNING) return WL_ERR_CANCELLED;
  return result == 0 ? WL_OK : WL_ERR_NO_DATA;
}
static wl_err_t wait_owner(void *context, uint32_t maximum_ms) {
  return wl_zephyr_uart_poll_wait(context, wl_zephyr_timeout_ms(maximum_ms));
}
static void notify_owner(void *context) { k_sem_give(&((wl_zephyr_uart_poll_t *)context)->wake); }
static int service(void *context) {
  wl_zephyr_uart_poll_t *a = context;
  (void)k_sem_take(&a->wake, K_NO_WAIT);
  return atomic_set(&a->error_pending, 0) ? WL_OK : WL_ERR_NO_DATA;
}
static wl_readiness_t readiness(const void *context, const wl_ctx_t *link, wl_time_ms_t now) {
  const wl_zephyr_uart_poll_t *a = context;
  (void)link; (void)now;
  wl_readiness_t hint = wl_readiness_idle();
  hint.runnable = atomic_get(&a->error_pending) != 0;
  return hint;
}
void wl_zephyr_uart_poll_request_stop(wl_zephyr_uart_poll_t *a) {
  if (a == NULL || !atomic_get(&a->initialized)) return;
  atomic_set(&a->running, STOPPED);
  k_sem_give(&a->stop_wake);
  k_sem_give(&a->wake);
}
void wl_zephyr_uart_poll_stop(wl_zephyr_uart_poll_t *a) {
  if (a == NULL || !atomic_get(&a->initialized)) return;
  __ASSERT(k_current_get() != &a->worker, "RX worker cannot join itself");
  wl_zephyr_uart_poll_request_stop(a);
  if (a->worker_created) {
    (void)k_thread_join(&a->worker, K_FOREVER);
    a->worker_created = false;
  }
  (void)wl_set_sink(a->link, NULL, NULL);
  a->notify = NULL;
  atomic_clear(&a->initialized);
}
static void quiesce(void *context) { wl_zephyr_uart_poll_stop(context); }
int wl_zephyr_uart_poll_attach(wl_zephyr_uart_poll_t *a, wl_endpoint_t *endpoint,
    const wl_zephyr_uart_poll_config_t *config) {
  if (a == NULL || config == NULL || config->uart == NULL || config->stack == NULL ||
      config->stack_size == 0 || config->priority < K_HIGHEST_THREAD_PRIO ||
      config->priority > K_LOWEST_APPLICATION_THREAD_PRIO || !device_is_ready(config->uart) ||
      wl_endpoint_link(endpoint) == NULL) return WL_ERR_INVALID_ARG;
  if (wl_endpoint_has_adapter(endpoint)) return WL_ERR_BUSY;
  if (config->wait_for_tx_idle) return WL_ERR_NOT_SUPPORTED;
  wl_config_t link_config;
  int result = wl_get_config(wl_endpoint_link(endpoint), &link_config);
  if (result != WL_OK) return result;
  if (link_config.envelope != WL_ENVELOPE_COBS_STREAM) return WL_ERR_NOT_SUPPORTED;
  const struct uart_driver_api *api = DEVICE_API_GET(uart, config->uart);
  if (api == NULL || api->poll_in == NULL || api->poll_out == NULL) return WL_ERR_NOT_SUPPORTED;
  memset(a, 0, sizeof(*a));
  a->uart = config->uart; a->link = wl_endpoint_link(endpoint);
  a->stack = config->stack; a->stack_size = config->stack_size; a->priority = config->priority;
  a->notify = config->notify; a->notify_user_data = config->notify_user_data;
  int initial_error = uart_err_check(a->uart);
  a->error_api_available = initial_error >= 0;
  if (initial_error > 0) note_error(a);
  k_sem_init(&a->wake, 0, 1); k_sem_init(&a->stop_wake, 0, 1);
  atomic_set(&a->initialized, 1);
  result = wl_set_tx_drain_required(a->link, false);
  if (result == WL_OK) result = wl_set_sink(a->link, sink, a);
  const wl_waiter_t waiter = {wait_owner, a, notify_owner};
  const wl_pump_hooks_t hooks = {.adapter_user_data = a, .service = service,
      .adapter_readiness = readiness, .quiesce = quiesce};
  if (result == WL_OK) result = wl_endpoint_set_waiter(endpoint, &waiter);
  if (result == WL_OK) result = wl_endpoint_attach(endpoint, &hooks);
  if (result != WL_OK) wl_zephyr_uart_poll_stop(a);
  return result;
}
int wl_zephyr_uart_poll_start(wl_zephyr_uart_poll_t *a) {
  if (a == NULL || !atomic_get(&a->initialized)) return WL_ERR_INVALID_ARG;
  /* A request before start, including a concurrent request at this CAS,
   * stays latched. Only a fresh attach resets STOPPED to READY. */
  if (!atomic_cas(&a->running, READY, RUNNING))
    return atomic_get(&a->running) == RUNNING ? WL_OK : WL_ERR_CANCELLED;
  a->worker_created = true;
  k_thread_create(&a->worker, a->stack, a->stack_size, receive, a, NULL, NULL,
      a->priority, 0, K_NO_WAIT);
  k_thread_name_set(&a->worker, "wl_uart_poll_rx");
  return WL_OK;
}
void wl_zephyr_uart_poll_get_stats(const wl_zephyr_uart_poll_t *a, wl_zephyr_uart_poll_stats_t *out) {
  if (a == NULL || out == NULL) return;
#define READ(field) out->field = (uint32_t)atomic_get(&a->field)
  READ(rx_bytes); READ(rx_pauses); READ(tx_bytes); READ(tx_consumed); READ(errors);
#undef READ
  out->started = atomic_get(&a->running) == RUNNING;
  out->tx_active = atomic_get(&a->tx_active) != 0;
  out->error_api_available = a->error_api_available;
}
