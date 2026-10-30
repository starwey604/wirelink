/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/uart_irq.h"
#include "wirelink/zephyr/detail/timeout.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <zephyr/drivers/uart.h>

#ifndef WL_ZEPHYR_UART_IRQ_HOT_CODE
#define WL_ZEPHYR_UART_IRQ_HOT_CODE
#endif

enum { INITIALIZED, STARTED, RX_PAUSED, TX_ACTIVE };
enum { TX_NONE, TX_CONSUMED, TX_FAILED };
enum { IRQ_PROGRESS = 1U, IRQ_NOTIFY = 2U };
/* Stable callback bindings survive adapter storage. A callback already
 * dispatched by the driver takes this lock before dereferencing an adapter.
 * Stop removes its binding under the same lock, covering SMP and late IRQs. */
#define UART_BINDINGS 8U
static struct k_spinlock callback_lock;
static struct { wl_zephyr_uart_irq_t *adapter; uintptr_t generation; } bindings[UART_BINDINGS];
static uintptr_t next_generation = UART_BINDINGS;

#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_NOTIFICATION_DIAGNOSTICS)
#define NOTE(adapter, field) ((void)atomic_inc(&(adapter)->field))
#else
#define NOTE(adapter, field) ((void)0)
#endif

static void WL_ZEPHYR_UART_IRQ_HOT_CODE wake_owner(wl_zephyr_uart_irq_t *adapter) {
  NOTE(adapter, callback_gives);
  k_sem_give(&adapter->wake);
  if (adapter->notify != NULL) adapter->notify(adapter->notify_user_data);
}
static wl_sink_result_t WL_ZEPHYR_UART_IRQ_HOT_CODE uart_sink(void *context, wl_io_token_t token,
                                  const uint8_t *data, size_t length) {
  wl_zephyr_uart_irq_t *adapter = context;
  if (data == NULL || length == 0U || !atomic_test_bit(&adapter->flags, STARTED))
    return WL_SINK_FAILED;
  k_spinlock_key_t key = k_spin_lock(&callback_lock);
  if (atomic_test_and_set_bit(&adapter->flags, TX_ACTIVE)) {
    k_spin_unlock(&callback_lock, key);
    return WL_SINK_BUSY;
  }
  adapter->tx_data = data;
  adapter->tx_length = length;
  adapter->tx_offset = 0U;
  adapter->tx_token = token;
  atomic_set(&adapter->tx_completion, TX_NONE);
  adapter->private_uart_api->irq_tx_enable(adapter->uart);
  k_spin_unlock(&callback_lock, key);
  return WL_SINK_STARTED;
}
static unsigned WL_ZEPHYR_UART_IRQ_HOT_CODE receive_ready(wl_zephyr_uart_irq_t *adapter) {
  wl_span_t span;
  int result = wl_rx_reserve(adapter->link, &span);
  if (result != WL_OK || span.length == 0U) {
    if (result == WL_OK) (void)wl_rx_commit(adapter->link, 0U);
    adapter->private_uart_api->irq_rx_disable(adapter->uart);
    atomic_set_bit(&adapter->flags, RX_PAUSED);
    atomic_inc(&adapter->rx_pauses);
    wl_rx_note_overflow(adapter->link);
    NOTE(adapter, publications);
    return IRQ_NOTIFY;
  }
  size_t request = span.length > INT_MAX ? INT_MAX : span.length;
  int received = adapter->private_uart_api->fifo_read(adapter->uart, span.data, (int)request);
  if (received <= 0) {
    (void)wl_rx_commit(adapter->link, 0U);
    if (received < 0) {
      atomic_inc(&adapter->errors);
      atomic_set(&adapter->error_pending, 1);
      NOTE(adapter, publications);
      return IRQ_NOTIFY;
    }
    return 0U;
  }
  if (wl_rx_commit(adapter->link, (size_t)received) != WL_OK)
    atomic_inc(&adapter->errors);
  else atomic_add(&adapter->rx_bytes, received);
  NOTE(adapter, publications);
  return IRQ_PROGRESS | IRQ_NOTIFY; /* Partial RX is a publication too. */
}
static unsigned WL_ZEPHYR_UART_IRQ_HOT_CODE transmit_ready(wl_zephyr_uart_irq_t *adapter) {
  const struct uart_driver_api *api = adapter->private_uart_api;
  if (!atomic_test_bit(&adapter->flags, TX_ACTIVE) ||
      atomic_get(&adapter->tx_completion) != TX_NONE || adapter->tx_data == NULL) {
    api->irq_tx_disable(adapter->uart);
    return 0U;
  }
  size_t remaining = adapter->tx_length - adapter->tx_offset;
  size_t request = remaining > INT_MAX ? INT_MAX : remaining;
  int sent = api->fifo_fill(adapter->uart, adapter->tx_data + adapter->tx_offset, (int)request);
  if (sent == 0) return 0U;
  if (sent < 0 || (size_t)sent > remaining) {
    api->irq_tx_disable(adapter->uart);
    adapter->tx_data = NULL;
    atomic_set(&adapter->tx_completion, TX_FAILED);
    atomic_inc(&adapter->errors);
    NOTE(adapter, publications);
    return IRQ_NOTIFY;
  }
  adapter->tx_offset += (size_t)sent;
  atomic_add(&adapter->tx_bytes, sent);
  if (adapter->tx_offset == adapter->tx_length) {
    api->irq_tx_disable(adapter->uart);
    adapter->tx_data = NULL; /* No driver access after consumed publication. */
    atomic_set(&adapter->tx_completion, TX_CONSUMED);
    NOTE(adapter, publications);
    return IRQ_NOTIFY;
  }
  return IRQ_PROGRESS;
}
static void WL_ZEPHYR_UART_IRQ_HOT_CODE uart_callback(const struct device *device, void *context) {
  (void)device;
  const uintptr_t generation = (uintptr_t)context;
  const size_t binding = generation & (UART_BINDINGS - 1U);
  k_spinlock_key_t key = k_spin_lock(&callback_lock);
  wl_zephyr_uart_irq_t *adapter = bindings[binding].adapter;
  if (bindings[binding].generation != generation || adapter == NULL || !atomic_test_bit(&adapter->flags, STARTED)) {
    k_spin_unlock(&callback_lock, key);
    return;
  }
  const struct uart_driver_api *api = adapter->private_uart_api;
  unsigned published = 0U;
  /* Bound each ISR even under continuous input. A ready source reasserts IRQ. */
#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_TIMING)
  const uint32_t callback_start = k_cycle_get_32();
  atomic_inc(&adapter->callback_irqs);
#endif
  for (unsigned i = 0U; i < 32U; ++i) {
    api->irq_update(adapter->uart);
    if (api->err_check != NULL && api->err_check(adapter->uart) > 0) {
      atomic_inc(&adapter->errors);
      atomic_set(&adapter->error_pending, 1);
      wl_rx_note_overflow(adapter->link);
      NOTE(adapter, publications);
      published |= IRQ_NOTIFY;
    }
    if (api->irq_is_pending(adapter->uart) <= 0) break;
    unsigned activity = 0U;
    if (!atomic_test_bit(&adapter->flags, RX_PAUSED) && api->irq_rx_ready(adapter->uart) > 0) {
      atomic_inc(&adapter->rx_irqs);
      activity |= receive_ready(adapter);
    }
    if (api->irq_tx_ready(adapter->uart) > 0) {
      atomic_inc(&adapter->tx_irqs);
      activity |= transmit_ready(adapter);
    }
    published |= activity;
    if (!(activity & IRQ_PROGRESS)) break;
  }
  /* Every valid exit joins here. Publish immediately, notify once per bounded
   * callback, while the binding lock still protects adapter/notifier lifetime.
   * No cross-callback pending flag: a later callback always gets a fresh give. */
  if (published & IRQ_NOTIFY) wake_owner(adapter);
#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_TIMING)
  atomic_add(&adapter->callback_ticks, (atomic_val_t)(k_cycle_get_32() - callback_start));
#endif
  k_spin_unlock(&callback_lock, key);
}
int wl_zephyr_uart_irq_init(wl_zephyr_uart_irq_t *adapter,
    const wl_zephyr_uart_irq_config_t *config) {
  wl_config_t link_config;
  if (adapter == NULL || config == NULL || config->uart == NULL ||
      config->link == NULL || !device_is_ready(config->uart) ||
      config->drain_check_ms > INT32_MAX) return WL_ERR_INVALID_ARG;
  int result = wl_get_config(config->link, &link_config);
  if (result != WL_OK) return result;
  if (link_config.envelope != WL_ENVELOPE_COBS_STREAM) return WL_ERR_NOT_SUPPORTED;
  const struct uart_driver_api *api = DEVICE_API_GET(uart, config->uart);
  if (api == NULL || api->irq_callback_set == NULL || api->irq_update == NULL ||
      api->irq_is_pending == NULL || api->irq_rx_ready == NULL || api->irq_tx_ready == NULL ||
      api->irq_rx_enable == NULL || api->irq_rx_disable == NULL ||
      api->irq_tx_enable == NULL || api->irq_tx_disable == NULL ||
      api->fifo_read == NULL || api->fifo_fill == NULL ||
      (config->wait_for_tx_idle && api->irq_tx_complete == NULL)) return WL_ERR_NOT_SUPPORTED;
  k_spinlock_key_t key = k_spin_lock(&callback_lock);
  size_t binding = UART_BINDINGS;
  for (size_t i = 0U; i < UART_BINDINGS; ++i) {
    if (bindings[i].adapter != NULL && bindings[i].adapter->uart == config->uart) {
      k_spin_unlock(&callback_lock, key);
      return WL_ERR_BUSY;
    }
    if (bindings[i].adapter == NULL) binding = i;
  }
  if (binding == UART_BINDINGS || next_generation > UINTPTR_MAX - UART_BINDINGS) { k_spin_unlock(&callback_lock, key); return WL_ERR_NO_SPACE; }
  memset(adapter, 0, sizeof(*adapter));
  adapter->uart = config->uart;
  adapter->link = config->link;
  adapter->private_uart_api = api;
  adapter->binding = (uint8_t)binding;
  adapter->wait_for_tx_idle = config->wait_for_tx_idle;
  adapter->drain_check_ms = config->drain_check_ms ? config->drain_check_ms : 1U;
  adapter->notify = config->notify;
  adapter->notify_user_data = config->notify_user_data;
  k_sem_init(&adapter->wake, 0, 1);
  bindings[binding].generation = next_generation | binding;
  next_generation += UART_BINDINGS;
  api->irq_callback_set(adapter->uart, uart_callback, (void *)bindings[binding].generation);
  bindings[binding].adapter = adapter;
  k_spin_unlock(&callback_lock, key);
  result = wl_set_tx_drain_required(adapter->link, config->wait_for_tx_idle);
  if (result == WL_OK) result = wl_set_sink(adapter->link, uart_sink, adapter);
  if (result != WL_OK) { wl_zephyr_uart_irq_stop(adapter); return result; }
  atomic_set_bit(&adapter->flags, INITIALIZED);
  return WL_OK;
}
int wl_zephyr_uart_irq_start(wl_zephyr_uart_irq_t *adapter) {
  if (adapter == NULL || !atomic_test_bit(&adapter->flags, INITIALIZED)) return WL_ERR_INVALID_ARG;
  k_spinlock_key_t key = k_spin_lock(&callback_lock);
  if (!atomic_test_and_set_bit(&adapter->flags, STARTED)) {
    adapter->private_uart_api->irq_rx_enable(adapter->uart);
    if (adapter->private_uart_api->irq_err_enable != NULL)
      adapter->private_uart_api->irq_err_enable(adapter->uart);
  }
  k_spin_unlock(&callback_lock, key);
  return WL_OK;
}
wl_readiness_t WL_ZEPHYR_UART_IRQ_HOT_CODE wl_zephyr_uart_irq_readiness(const void *context,
    const wl_ctx_t *link, wl_time_ms_t now_ms) {
  const wl_zephyr_uart_irq_t *adapter = context;
  wl_readiness_t value = wl_readiness_idle();
  if (!atomic_test_bit(&adapter->flags, STARTED)) return value;
  value.runnable = atomic_get(&adapter->error_pending) || atomic_get(&adapter->tx_completion) != TX_NONE ||
      (atomic_test_bit(&adapter->flags, RX_PAUSED) && wl_rx_has_space(link));
  value.blocked = atomic_test_bit(&adapter->flags, TX_ACTIVE) ||
      atomic_test_bit(&adapter->flags, RX_PAUSED);
  if (adapter->drain_pending) {
    uint32_t remaining = (int32_t)(now_ms - adapter->drain_due) >= 0
        ? 0U : adapter->drain_due - now_ms;
    wl_readiness_merge(&value, wl_readiness_at(remaining));
  }
  return value;
}
int WL_ZEPHYR_UART_IRQ_HOT_CODE wl_zephyr_uart_irq_service(wl_zephyr_uart_irq_t *adapter, wl_time_ms_t now_ms) {
  if (adapter == NULL || !atomic_test_bit(&adapter->flags, STARTED)) return WL_ERR_INVALID_ARG;
  /* Acknowledge the latch before observing published input. An interrupt after
   * this point leaves a fresh latch; buffered input is polled later this pass. */
  if (k_sem_take(&adapter->wake, K_NO_WAIT) == 0) NOTE(adapter, service_acks);
  int result = atomic_set(&adapter->error_pending, 0) ? WL_OK : WL_ERR_NO_DATA;
  k_spinlock_key_t tx_key = k_spin_lock(&callback_lock);
  atomic_val_t completion = atomic_set(&adapter->tx_completion, TX_NONE);
  wl_io_token_t consumed_token = adapter->tx_token;
  if (completion != TX_NONE) {
    adapter->tx_length = adapter->tx_offset = 0U;
    atomic_inc(&adapter->tx_consumed);
    if (completion == TX_CONSUMED && adapter->wait_for_tx_idle) {
      adapter->drain_pending = 1U;
      adapter->drain_due = now_ms;
    } else {
      adapter->tx_token = 0U;
      atomic_clear_bit(&adapter->flags, TX_ACTIVE);
    }
  }
  k_spin_unlock(&callback_lock, tx_key);
  if (completion != TX_NONE) {
    result = wl_tx_consumed(adapter->link, consumed_token, completion == TX_CONSUMED ? WL_OK : WL_ERR_IO);
    if (result != WL_OK) atomic_inc(&adapter->errors);
  }
  if (adapter->drain_pending && (int32_t)(now_ms - adapter->drain_due) >= 0) {
    atomic_inc(&adapter->drain_queries);
    int idle = adapter->private_uart_api->irq_tx_complete(adapter->uart);
    if (idle != 0) {
      wl_io_token_t token = adapter->tx_token;
      adapter->drain_pending = 0U;
      adapter->tx_token = 0U;
      atomic_clear_bit(&adapter->flags, TX_ACTIVE);
      if (idle > 0) atomic_inc(&adapter->tx_drained);
      else atomic_inc(&adapter->errors); /* -ENOSYS is never a fake drain. */
      result = wl_tx_drained(adapter->link, token, idle > 0 ? WL_OK : WL_ERR_IO);
    } else adapter->drain_due = now_ms + adapter->drain_check_ms;
  }
  if (atomic_test_bit(&adapter->flags, RX_PAUSED) && wl_rx_has_space(adapter->link)) {
    k_spinlock_key_t key = k_spin_lock(&callback_lock);
    if (atomic_test_and_clear_bit(&adapter->flags, RX_PAUSED))
      adapter->private_uart_api->irq_rx_enable(adapter->uart);
    k_spin_unlock(&callback_lock, key);
    result = WL_OK;
  }
  return result;
}
wl_err_t WL_ZEPHYR_UART_IRQ_HOT_CODE wl_zephyr_uart_irq_wait(wl_zephyr_uart_irq_t *adapter, k_timeout_t timeout) {
  if (adapter == NULL) return WL_ERR_INVALID_ARG;
  if (!atomic_test_bit(&adapter->flags, STARTED)) return WL_ERR_CANCELLED;
  NOTE(adapter, waits);
#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_NOTIFICATION_DIAGNOSTICS)
  int result = k_sem_take(&adapter->wake, K_NO_WAIT);
  if (result == 0) NOTE(adapter, wait_ready);
  else result = k_sem_take(&adapter->wake, timeout);
#else
  int result = k_sem_take(&adapter->wake, timeout);
#endif
  if (!atomic_test_bit(&adapter->flags, STARTED)) return WL_ERR_CANCELLED;
  return result == 0 ? WL_OK : WL_ERR_NO_DATA;
}
static wl_err_t WL_ZEPHYR_UART_IRQ_HOT_CODE wait_owner(void *context, uint32_t maximum_ms) {
  return wl_zephyr_uart_irq_wait(context, wl_zephyr_timeout_ms(maximum_ms));
}
static void WL_ZEPHYR_UART_IRQ_HOT_CODE notify_owner(void *context) { k_sem_give(&((wl_zephyr_uart_irq_t *)context)->wake); }
const wl_waiter_t *wl_zephyr_uart_irq_waiter(wl_zephyr_uart_irq_t *adapter, wl_waiter_t *out) {
  if (adapter == NULL || out == NULL) return NULL;
  *out = (wl_waiter_t){wait_owner, adapter, notify_owner};
  return out;
}
static int WL_ZEPHYR_UART_IRQ_HOT_CODE service_owner(void *context) {
  wl_zephyr_uart_irq_t *adapter = context;
  wl_time_ms_t now_ms;
  int result = wl_endpoint_now(adapter->endpoint, &now_ms);
  return result == WL_OK ? wl_zephyr_uart_irq_service(adapter, now_ms) : result;
}
static void stop_owner(void *context) { wl_zephyr_uart_irq_stop(context); }
int wl_zephyr_uart_irq_attach(wl_zephyr_uart_irq_t *adapter,
    wl_endpoint_t *endpoint, const wl_zephyr_uart_irq_config_t *config) {
  if (config == NULL || wl_endpoint_link(endpoint) == NULL) return WL_ERR_INVALID_ARG;
  if (wl_endpoint_has_adapter(endpoint)) return WL_ERR_BUSY;
  wl_zephyr_uart_irq_config_t port = *config;
  port.link = wl_endpoint_link(endpoint);
  int result = wl_zephyr_uart_irq_init(adapter, &port);
  if (result != WL_OK) return result;
  adapter->endpoint = endpoint;
  wl_waiter_t waiter;
  wl_zephyr_uart_irq_waiter(adapter, &waiter);
  const wl_pump_hooks_t hooks = {.adapter_user_data = adapter, .service = service_owner,
    .quiesce = stop_owner, .adapter_readiness = wl_zephyr_uart_irq_readiness};
  result = wl_endpoint_set_waiter(endpoint, &waiter);
  if (result == WL_OK) result = wl_endpoint_attach(endpoint, &hooks);
  if (result != WL_OK) wl_zephyr_uart_irq_stop(adapter);
  return result;
}
void wl_zephyr_uart_irq_stop(wl_zephyr_uart_irq_t *adapter) {
  if (adapter == NULL || adapter->uart == NULL) return;
  k_spinlock_key_t key = k_spin_lock(&callback_lock);
  if (bindings[adapter->binding].adapter != adapter) {
    k_spin_unlock(&callback_lock, key);
    return;
  }
  atomic_clear_bit(&adapter->flags, STARTED);
  adapter->private_uart_api->irq_rx_disable(adapter->uart);
  adapter->private_uart_api->irq_tx_disable(adapter->uart);
  if (adapter->private_uart_api->irq_err_disable != NULL)
    adapter->private_uart_api->irq_err_disable(adapter->uart);
  adapter->private_uart_api->irq_callback_set(adapter->uart, NULL, NULL);
  if (bindings[adapter->binding].adapter == adapter) bindings[adapter->binding].adapter = NULL;
  adapter->notify = NULL;
  k_spin_unlock(&callback_lock, key);
  (void)wl_set_sink(adapter->link, NULL, NULL);
  if (atomic_test_and_clear_bit(&adapter->flags, TX_ACTIVE)) {
    if (adapter->drain_pending) (void)wl_tx_drained(adapter->link, adapter->tx_token, WL_ERR_IO);
    else (void)wl_tx_consumed(adapter->link, adapter->tx_token, WL_ERR_IO);
  }
  adapter->tx_data = NULL;
  adapter->tx_token = 0U;
  adapter->drain_pending = 0U;
  atomic_clear_bit(&adapter->flags, INITIALIZED);
  k_sem_give(&adapter->wake);
}
void wl_zephyr_uart_irq_get_stats(const wl_zephyr_uart_irq_t *adapter,
    wl_zephyr_uart_irq_stats_t *out) {
  if (adapter == NULL || out == NULL) return;
#define READ(field) out->field = (uint32_t)atomic_get(&adapter->field)
  READ(rx_irqs); READ(rx_bytes); READ(rx_pauses); READ(tx_irqs); READ(tx_bytes);
  READ(tx_consumed); READ(tx_drained); READ(drain_queries); READ(errors);
#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_NOTIFICATION_DIAGNOSTICS)
  READ(publications); READ(callback_gives); READ(service_acks); READ(waits); READ(wait_ready);
#endif
#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_TIMING)
  READ(callback_irqs); READ(callback_ticks);
#endif
#undef READ
  out->started = atomic_test_bit(&adapter->flags, STARTED);
  out->rx_paused = atomic_test_bit(&adapter->flags, RX_PAUSED);
  out->tx_active = atomic_test_bit(&adapter->flags, TX_ACTIVE);
}
