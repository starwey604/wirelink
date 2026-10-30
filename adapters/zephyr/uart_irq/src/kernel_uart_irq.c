/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/kernel_uart_irq.h"
#include "wirelink/zephyr/kernel_timeout.h"
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
static struct {
  wl_kernel_uart_irq_t *adapter;
  uintptr_t generation;
} bindings[UART_BINDINGS];
static uintptr_t next_generation = UART_BINDINGS;

#if defined(CONFIG_WIRELINK_KERNEL_ZEPHYR_UART_IRQ_NOTIFICATION_DIAGNOSTICS)
#define NOTE(adapter, field) ((void)atomic_inc(&(adapter)->field))
#else
#define NOTE(adapter, field) ((void)0)
#endif

static void WL_ZEPHYR_UART_IRQ_HOT_CODE wake_owner(wl_kernel_uart_irq_t *adapter) {
  NOTE(adapter, callback_gives);
  k_sem_give(&adapter->wake);
  if (adapter->notify != NULL)
    adapter->notify(adapter->notify_user_data);
}
wl_packet_submit_t wl_kernel_uart_irq_submit(void *context, const uint8_t *data, size_t length,
                                             wl_packet_token_t token) {
  wl_kernel_uart_irq_t *adapter = context;
  if (data == NULL || length == 0U || !atomic_test_bit(&adapter->flags, STARTED))
    return WL_PACKET_FAILED;
  k_spinlock_key_t key = k_spin_lock(&callback_lock);
  if (atomic_test_and_set_bit(&adapter->flags, TX_ACTIVE)) {
    k_spin_unlock(&callback_lock, key);
    return WL_PACKET_BUSY;
  }
  adapter->tx_data = data;
  adapter->tx_length = length;
  adapter->tx_offset = 0U;
  adapter->tx_token = token;
  atomic_set(&adapter->tx_completion, TX_NONE);
  adapter->private_uart_api->irq_tx_enable(adapter->uart);
  k_spin_unlock(&callback_lock, key);
  return WL_PACKET_STARTED;
}
static unsigned WL_ZEPHYR_UART_IRQ_HOT_CODE receive_ready(wl_kernel_uart_irq_t *adapter) {
  wl_span_t span;
  int result = wl_byte_fifo_reserve(adapter->fifo, &span);
  if (result != WL_OK || span.length == 0U) {
    if (result == WL_OK)
      (void)wl_byte_fifo_commit(adapter->fifo, 0U);
    adapter->private_uart_api->irq_rx_disable(adapter->uart);
    atomic_set_bit(&adapter->flags, RX_PAUSED);
    atomic_inc(&adapter->rx_pauses);
    NOTE(adapter, publications);
    return IRQ_NOTIFY;
  }
  size_t request = span.length > INT_MAX ? INT_MAX : span.length;
  int received = adapter->private_uart_api->fifo_read(adapter->uart, span.data, (int)request);
  if (received <= 0) {
    (void)wl_byte_fifo_commit(adapter->fifo, 0U);
    if (received < 0) {
      atomic_inc(&adapter->errors);
      atomic_set(&adapter->error_pending, 1);
      NOTE(adapter, publications);
      return IRQ_NOTIFY;
    }
    return 0U;
  }
  if (wl_byte_fifo_commit(adapter->fifo, (size_t)received) != WL_OK)
    atomic_inc(&adapter->errors);
  else
    atomic_add(&adapter->rx_bytes, received);
  NOTE(adapter, publications);
  return IRQ_PROGRESS | IRQ_NOTIFY; /* Partial RX is a publication too. */
}
static unsigned WL_ZEPHYR_UART_IRQ_HOT_CODE transmit_ready(wl_kernel_uart_irq_t *adapter) {
  const struct uart_driver_api *api = adapter->private_uart_api;
  if (!atomic_test_bit(&adapter->flags, TX_ACTIVE) ||
      atomic_get(&adapter->tx_completion) != TX_NONE || adapter->tx_data == NULL) {
    api->irq_tx_disable(adapter->uart);
    return 0U;
  }
  size_t remaining = adapter->tx_length - adapter->tx_offset;
  size_t request = remaining > INT_MAX ? INT_MAX : remaining;
  int sent = api->fifo_fill(adapter->uart, adapter->tx_data + adapter->tx_offset, (int)request);
  if (sent == 0)
    return 0U;
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
  wl_kernel_uart_irq_t *adapter = bindings[binding].adapter;
  if (bindings[binding].generation != generation || adapter == NULL ||
      !atomic_test_bit(&adapter->flags, STARTED)) {
    k_spin_unlock(&callback_lock, key);
    return;
  }
  const struct uart_driver_api *api = adapter->private_uart_api;
  unsigned published = 0U;
  /* Bound each ISR even under continuous input. A ready source reasserts IRQ. */
#if defined(CONFIG_WIRELINK_KERNEL_ZEPHYR_UART_IRQ_TIMING)
  const uint32_t callback_start = k_cycle_get_32();
  atomic_inc(&adapter->callback_irqs);
#endif
  for (unsigned i = 0U; i < 32U; ++i) {
    api->irq_update(adapter->uart);
    if (api->err_check != NULL && api->err_check(adapter->uart) > 0) {
      atomic_inc(&adapter->errors);
      atomic_set(&adapter->error_pending, 1);
      wl_byte_fifo_note_gap(adapter->fifo);
      NOTE(adapter, publications);
      published |= IRQ_NOTIFY;
    }
    if (api->irq_is_pending(adapter->uart) <= 0)
      break;
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
    if (!(activity & IRQ_PROGRESS))
      break;
  }
  /* Every valid exit joins here. Publish immediately, notify once per bounded
   * callback, while the binding lock still protects adapter/notifier lifetime.
   * No cross-callback pending flag: a later callback always gets a fresh give. */
  if (published & IRQ_NOTIFY)
    wake_owner(adapter);
#if defined(CONFIG_WIRELINK_KERNEL_ZEPHYR_UART_IRQ_TIMING)
  atomic_add(&adapter->callback_ticks, (atomic_val_t)(k_cycle_get_32() - callback_start));
#endif
  k_spin_unlock(&callback_lock, key);
}
int wl_kernel_uart_irq_init(wl_kernel_uart_irq_t *adapter,
                            const wl_kernel_uart_irq_config_t *config) {
  if (adapter == NULL || config == NULL || config->uart == NULL || config->port == NULL ||
      config->fifo == NULL || config->stream == NULL || !device_is_ready(config->uart) ||
      config->drain_check_ms > INT32_MAX)
    return WL_ERR_INVALID_ARG;
  const struct uart_driver_api *api = DEVICE_API_GET(uart, config->uart);
  if (api == NULL || api->irq_callback_set == NULL || api->irq_update == NULL ||
      api->irq_is_pending == NULL || api->irq_rx_ready == NULL || api->irq_tx_ready == NULL ||
      api->irq_rx_enable == NULL || api->irq_rx_disable == NULL || api->irq_tx_enable == NULL ||
      api->irq_tx_disable == NULL || api->fifo_read == NULL || api->fifo_fill == NULL ||
      api->irq_tx_complete == NULL)
    return WL_ERR_NOT_SUPPORTED;
  k_spinlock_key_t key = k_spin_lock(&callback_lock);
  size_t binding = UART_BINDINGS;
  for (size_t i = 0U; i < UART_BINDINGS; ++i) {
    if (bindings[i].adapter != NULL && bindings[i].adapter->uart == config->uart) {
      k_spin_unlock(&callback_lock, key);
      return WL_ERR_BUSY;
    }
    if (bindings[i].adapter == NULL)
      binding = i;
  }
  if (binding == UART_BINDINGS || next_generation > UINTPTR_MAX - UART_BINDINGS) {
    k_spin_unlock(&callback_lock, key);
    return WL_ERR_NO_SPACE;
  }
  memset(adapter, 0, sizeof(*adapter));
  adapter->uart = config->uart;
  adapter->port = config->port;
  adapter->fifo = config->fifo;
  adapter->stream = config->stream;
  adapter->private_uart_api = api;
  adapter->binding = (uint8_t)binding;
  adapter->wait_for_tx_idle = 1U;
  adapter->drain_check_ms = config->drain_check_ms ? config->drain_check_ms : 1U;
  adapter->notify = config->notify;
  adapter->notify_user_data = config->notify_user_data;
  k_sem_init(&adapter->wake, 0, 1);
  bindings[binding].generation = next_generation | binding;
  next_generation += UART_BINDINGS;
  api->irq_callback_set(adapter->uart, uart_callback, (void *)bindings[binding].generation);
  bindings[binding].adapter = adapter;
  k_spin_unlock(&callback_lock, key);
  atomic_set_bit(&adapter->flags, INITIALIZED);
  return WL_OK;
}
int wl_kernel_uart_irq_start(wl_kernel_uart_irq_t *adapter) {
  if (adapter == NULL || !atomic_test_bit(&adapter->flags, INITIALIZED))
    return WL_ERR_INVALID_ARG;
  k_spinlock_key_t key = k_spin_lock(&callback_lock);
  if (!atomic_test_and_set_bit(&adapter->flags, STARTED)) {
    adapter->private_uart_api->irq_rx_enable(adapter->uart);
    if (adapter->private_uart_api->irq_err_enable != NULL)
      adapter->private_uart_api->irq_err_enable(adapter->uart);
  }
  k_spin_unlock(&callback_lock, key);
  return WL_OK;
}
wl_io_readiness_t WL_ZEPHYR_UART_IRQ_HOT_CODE wl_kernel_uart_irq_readiness(const void *context,
                                                                           uint32_t now_ms) {
  const wl_kernel_uart_irq_t *adapter = context;
  wl_io_readiness_t value = wl_io_readiness_idle();
  if (!atomic_test_bit(&adapter->flags, STARTED))
    return value;
  value.runnable =
      wl_byte_fifo_cobs_ready(adapter->fifo, adapter->stream) ||
      atomic_get(&adapter->error_pending) || atomic_get(&adapter->tx_completion) != TX_NONE ||
      (atomic_test_bit(&adapter->flags, RX_PAUSED) && wl_byte_fifo_has_space(adapter->fifo));
  value.blocked =
      atomic_test_bit(&adapter->flags, TX_ACTIVE) || atomic_test_bit(&adapter->flags, RX_PAUSED);
  if (adapter->drain_pending) {
    uint32_t remaining =
        (int32_t)(now_ms - adapter->drain_due) >= 0 ? 0U : adapter->drain_due - now_ms;
    wl_io_readiness_merge(&value, wl_io_readiness_at(remaining));
  }
  return value;
}
int WL_ZEPHYR_UART_IRQ_HOT_CODE wl_kernel_uart_irq_service(wl_kernel_uart_irq_t *adapter,
                                                           uint32_t now_ms) {
  if (adapter == NULL || !atomic_test_bit(&adapter->flags, STARTED))
    return WL_ERR_INVALID_ARG;
  /* Acknowledge the latch before observing published input. An interrupt after
   * this point leaves a fresh latch; buffered input is polled later this pass. */
  if (k_sem_take(&adapter->wake, K_NO_WAIT) == 0)
    NOTE(adapter, service_acks);
  int result = atomic_set(&adapter->error_pending, 0) ? WL_ERR_IO : WL_OK;
  k_spinlock_key_t tx_key = k_spin_lock(&callback_lock);
  atomic_val_t completion = atomic_set(&adapter->tx_completion, TX_NONE);
  wl_packet_token_t consumed_token = adapter->tx_token;
  if (completion != TX_NONE) {
    adapter->tx_length = adapter->tx_offset = 0U;
    atomic_inc(&adapter->tx_consumed);
    if (completion == TX_CONSUMED && adapter->wait_for_tx_idle) {
      adapter->drain_pending = 1U;
      adapter->drain_due = now_ms;
    } else {
      adapter->tx_token = (wl_packet_token_t){0};
      atomic_clear_bit(&adapter->flags, TX_ACTIVE);
    }
  }
  k_spin_unlock(&callback_lock, tx_key);
  if (completion != TX_NONE) {
    result =
        wl_packet_tx_publish(adapter->port, consumed_token,
                             completion == TX_CONSUMED ? WL_PACKET_CONSUMED : WL_PACKET_TX_ERROR);
    if (result != WL_OK)
      atomic_inc(&adapter->errors);
  }
  if (adapter->drain_pending && (int32_t)(now_ms - adapter->drain_due) >= 0) {
    atomic_inc(&adapter->drain_queries);
    int idle = adapter->private_uart_api->irq_tx_complete(adapter->uart);
    if (idle != 0) {
      wl_packet_token_t token = adapter->tx_token;
      adapter->drain_pending = 0U;
      adapter->tx_token = (wl_packet_token_t){0};
      atomic_clear_bit(&adapter->flags, TX_ACTIVE);
      if (idle > 0)
        atomic_inc(&adapter->tx_drained);
      else
        atomic_inc(&adapter->errors); /* -ENOSYS is never a fake drain. */
      result = wl_packet_tx_publish(adapter->port, token,
                                    idle > 0 ? WL_PACKET_DRAINED : WL_PACKET_TX_ERROR);
    } else
      adapter->drain_due = now_ms + adapter->drain_check_ms;
  }
  if (atomic_test_bit(&adapter->flags, RX_PAUSED) && wl_byte_fifo_has_space(adapter->fifo)) {
    k_spinlock_key_t key = k_spin_lock(&callback_lock);
    if (atomic_test_and_clear_bit(&adapter->flags, RX_PAUSED))
      adapter->private_uart_api->irq_rx_enable(adapter->uart);
    k_spin_unlock(&callback_lock, key);
    result = WL_OK;
  }
  int input = wl_byte_fifo_to_cobs(adapter->fifo, adapter->stream, 16U);
  if (input != WL_OK && input != WL_ERR_WOULD_BLOCK)
    result = input;
  return result;
}
wl_err_t WL_ZEPHYR_UART_IRQ_HOT_CODE wl_kernel_uart_irq_wait(wl_kernel_uart_irq_t *adapter,
                                                             k_timeout_t timeout) {
  if (adapter == NULL)
    return WL_ERR_INVALID_ARG;
  if (!atomic_test_bit(&adapter->flags, STARTED))
    return WL_ERR_CANCELLED;
  NOTE(adapter, waits);
#if defined(CONFIG_WIRELINK_KERNEL_ZEPHYR_UART_IRQ_NOTIFICATION_DIAGNOSTICS)
  int result = k_sem_take(&adapter->wake, K_NO_WAIT);
  if (result == 0)
    NOTE(adapter, wait_ready);
  else
    result = k_sem_take(&adapter->wake, timeout);
#else
  int result = k_sem_take(&adapter->wake, timeout);
#endif
  if (!atomic_test_bit(&adapter->flags, STARTED))
    return WL_ERR_CANCELLED;
  return result == 0 ? WL_OK : WL_ERR_NO_DATA;
}
void wl_kernel_uart_irq_notify(void *context) {
  k_sem_give(&((wl_kernel_uart_irq_t *)context)->wake);
}
void wl_kernel_uart_irq_stop(wl_kernel_uart_irq_t *adapter) {
  if (adapter == NULL || adapter->uart == NULL)
    return;
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
  if (bindings[adapter->binding].adapter == adapter)
    bindings[adapter->binding].adapter = NULL;
  adapter->notify = NULL;
  k_spin_unlock(&callback_lock, key);
  wl_packet_stop(adapter->port);
  (void)wl_packet_stream_abort_rx(adapter->stream);
  if (atomic_test_and_clear_bit(&adapter->flags, TX_ACTIVE)) {
    (void)wl_packet_tx_publish(adapter->port, adapter->tx_token, WL_PACKET_TX_ERROR);
  }
  adapter->tx_data = NULL;
  adapter->tx_token = (wl_packet_token_t){0};
  adapter->drain_pending = 0U;
  atomic_clear_bit(&adapter->flags, INITIALIZED);
  k_sem_give(&adapter->wake);
}
void wl_kernel_uart_irq_get_stats(const wl_kernel_uart_irq_t *adapter,
                                  wl_kernel_uart_irq_stats_t *out) {
  if (adapter == NULL || out == NULL)
    return;
#define READ(field) out->field = (uint32_t)atomic_get(&adapter->field)
  READ(rx_irqs);
  READ(rx_bytes);
  READ(rx_pauses);
  READ(tx_irqs);
  READ(tx_bytes);
  READ(tx_consumed);
  READ(tx_drained);
  READ(drain_queries);
  READ(errors);
#if defined(CONFIG_WIRELINK_KERNEL_ZEPHYR_UART_IRQ_NOTIFICATION_DIAGNOSTICS)
  READ(publications);
  READ(callback_gives);
  READ(service_acks);
  READ(waits);
  READ(wait_ready);
#endif
#if defined(CONFIG_WIRELINK_KERNEL_ZEPHYR_UART_IRQ_TIMING)
  READ(callback_irqs);
  READ(callback_ticks);
#endif
#undef READ
  out->started = atomic_test_bit(&adapter->flags, STARTED);
  out->rx_paused = atomic_test_bit(&adapter->flags, RX_PAUSED);
  out->tx_active = atomic_test_bit(&adapter->flags, TX_ACTIVE);
}
