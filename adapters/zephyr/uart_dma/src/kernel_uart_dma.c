/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/zephyr/kernel_uart_dma.h"
#include "wirelink/zephyr/kernel_timeout.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

enum {
  TX_COMPLETION_NONE = 0,
  TX_COMPLETION_DONE,
  TX_COMPLETION_ABORTED,
};

enum { RX_SLOT_COUNT = 2U };

static bool spans_overlap(const wl_span_t *left, const wl_span_t *right) {
  const uintptr_t left_address = (uintptr_t)left->data;
  const uintptr_t right_address = (uintptr_t)right->data;

  return left_address <= right_address ? right_address - left_address < left->length
                                       : left_address - right_address < right->length;
}

static void reset_slot(wl_kernel_uart_dma_slot_t *slot) {
  slot->received = 0U;
  slot->forwarded = 0U;
  slot->order = 0U;
  slot->driver_owned = 0U;
  slot->released = 0U;
}

static wl_kernel_uart_dma_slot_t *find_slot(wl_kernel_uart_dma_t *adapter, uint8_t *data) {
  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    wl_kernel_uart_dma_slot_t *slot = &adapter->slots[i];

    if (slot->order != 0U && slot->buffer.data == data) {
      return slot;
    }
  }
  return NULL;
}

static wl_kernel_uart_dma_slot_t *find_free_slot(wl_kernel_uart_dma_t *adapter) {
  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    if (adapter->slots[i].order == 0U) {
      return &adapter->slots[i];
    }
  }
  return NULL;
}

static bool driver_owns_any_slot(wl_kernel_uart_dma_t *adapter) {
  bool owned = false;
  k_spinlock_key_t key = k_spin_lock(&adapter->rx_lock);
  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    if (adapter->slots[i].driver_owned != 0U) {
      owned = true;
      break;
    }
  }
  k_spin_unlock(&adapter->rx_lock, key);
  return owned;
}

static bool any_slot_is_active(wl_kernel_uart_dma_t *adapter) {
  bool active = false;
  k_spinlock_key_t key = k_spin_lock(&adapter->rx_lock);
  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    if (adapter->slots[i].order != 0U) {
      active = true;
      break;
    }
  }
  k_spin_unlock(&adapter->rx_lock, key);
  return active;
}

static void mark_abort(wl_kernel_uart_dma_t *adapter) {
  atomic_set(&adapter->abort_pending, 1);
  atomic_inc(&adapter->errors);
}

static wl_kernel_uart_dma_slot_t *oldest_pending_slot(wl_kernel_uart_dma_t *adapter) {
  wl_kernel_uart_dma_slot_t *oldest = NULL;

  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    wl_kernel_uart_dma_slot_t *slot = &adapter->slots[i];

    if (slot->order != 0U && slot->forwarded < slot->received &&
        (oldest == NULL || slot->order < oldest->order)) {
      oldest = slot;
    }
  }
  return oldest;
}

static void retire_forwarded_slots(wl_kernel_uart_dma_t *adapter) {
  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    wl_kernel_uart_dma_slot_t *slot = &adapter->slots[i];

    if (slot->order != 0U && slot->released != 0U && slot->forwarded == slot->received) {
      reset_slot(slot);
    }
  }
}

/* rx_lock makes callback and owner service one logical SPSC producer. Bytes
 * remain in a released staging slot when the ring is full; that is
 * backpressure, not an overflow, until the UART actually has to stop. */
static int drain_ready_slots_locked(wl_kernel_uart_dma_t *adapter) {
  /* Shutdown discards unpublished staging bytes after driver ownership ends.
   * The closing owner must not have to consume the core ring to return them. */
  if (atomic_get(&adapter->stopping) != 0) {
    return WL_OK;
  }
  for (;;) {
    wl_kernel_uart_dma_slot_t *slot;
    wl_span_t span = {0};
    size_t length;
    int ret;

    retire_forwarded_slots(adapter);
    slot = oldest_pending_slot(adapter);
    if (slot == NULL) {
      return WL_OK;
    }
    ret = wl_byte_fifo_reserve(adapter->config.fifo, &span);
    if (ret != WL_OK) {
      return ret;
    }
    if (span.length == 0U) {
      (void)wl_byte_fifo_commit(adapter->config.fifo, 0U);
      return WL_ERR_WOULD_BLOCK;
    }
    length = slot->received - slot->forwarded;
    if (length > span.length) {
      length = span.length;
    }
    memcpy(span.data, slot->buffer.data + slot->forwarded, length);
    ret = wl_byte_fifo_commit(adapter->config.fifo, length);
    if (ret != WL_OK) {
      return ret;
    }
    slot->forwarded += length;
    atomic_add(&adapter->published_bytes, (atomic_val_t)length);
  }
}

static wl_kernel_uart_dma_slot_t *arm_free_slot_locked(wl_kernel_uart_dma_t *adapter) {
  wl_kernel_uart_dma_slot_t *slot;

  retire_forwarded_slots(adapter);
  slot = find_free_slot(adapter);
  if (slot == NULL) {
    return NULL;
  }
  if (++adapter->rx_next_order == 0U) {
    ++adapter->rx_next_order;
  }
  slot->received = 0U;
  slot->forwarded = 0U;
  slot->order = adapter->rx_next_order;
  slot->driver_owned = 1U;
  slot->released = 0U;
  return slot;
}

static int submit_requested_buffer(wl_kernel_uart_dma_t *adapter) {
  wl_kernel_uart_dma_slot_t *slot;
  k_spinlock_key_t key;
  int ret;

  key = k_spin_lock(&adapter->rx_lock);
  if (adapter->buffer_request_pending == 0U || atomic_get(&adapter->stopping) != 0 ||
      atomic_get(&adapter->abort_pending) != 0) {
    k_spin_unlock(&adapter->rx_lock, key);
    return WL_OK;
  }
  slot = arm_free_slot_locked(adapter);
  if (slot == NULL) {
    atomic_set(&adapter->paused, 1);
    k_spin_unlock(&adapter->rx_lock, key);
    return WL_ERR_WOULD_BLOCK;
  }
  adapter->buffer_request_pending = 0U;
  k_spin_unlock(&adapter->rx_lock, key);

  if (adapter->config.prepare_for_dma != NULL) {
    adapter->config.prepare_for_dma(adapter->config.cache_user_data, slot->buffer.data,
                                    slot->buffer.length);
  }
  ret = uart_rx_buf_rsp(adapter->config.uart, slot->buffer.data, slot->buffer.length);
  if (ret != 0) {
    key = k_spin_lock(&adapter->rx_lock);
    reset_slot(slot);
    k_spin_unlock(&adapter->rx_lock, key);
    mark_abort(adapter);
    return WL_ERR_IO;
  }
  atomic_set(&adapter->paused, 0);
  return WL_OK;
}

wl_packet_submit_t wl_kernel_uart_dma_submit(void *user_data, const uint8_t *data, size_t length,
                                             wl_packet_token_t token) {
  wl_kernel_uart_dma_t *adapter = user_data;
  int ret;

  if (adapter == NULL || data == NULL || length == 0U || atomic_get(&adapter->started) == 0 ||
      atomic_get(&adapter->stopping) != 0) {
    return WL_PACKET_FAILED;
  }
  k_spinlock_key_t tx_key = k_spin_lock(&adapter->tx_lock);
  if (!atomic_cas(&adapter->tx_active, 0, 1)) {
    k_spin_unlock(&adapter->tx_lock, tx_key);
    atomic_inc(&adapter->tx_busy);
    return WL_PACKET_BUSY;
  }
  adapter->tx_token = token;
  adapter->tx_data = data;
  adapter->tx_length = length;
  atomic_set(&adapter->tx_completion, TX_COMPLETION_NONE);

  k_spin_unlock(&adapter->tx_lock, tx_key);
  ret = uart_tx(adapter->config.uart, data, length, adapter->config.tx_timeout_us);
  if (ret == 0) {
    atomic_inc(&adapter->tx_submissions);
    return WL_PACKET_STARTED;
  }

  tx_key = k_spin_lock(&adapter->tx_lock);
  adapter->tx_token = (wl_packet_token_t){0};
  adapter->tx_data = NULL;
  adapter->tx_length = 0U;
  atomic_set(&adapter->tx_active, 0);
  k_spin_unlock(&adapter->tx_lock, tx_key);
  if (ret == -EBUSY || ret == -EAGAIN) {
    atomic_inc(&adapter->tx_busy);
    uint32_t now = k_uptime_get_32();
    adapter->tx_retry_due = now + (adapter->config.tx_retry_ms ? adapter->config.tx_retry_ms : 1U);
    adapter->tx_retry_pending = 1U;
    return WL_PACKET_BUSY;
  }
  atomic_inc(&adapter->errors);
  return WL_PACKET_FAILED;
}

#define DMA_BINDINGS 8U
static struct k_spinlock dma_binding_lock;
static struct {
  wl_kernel_uart_dma_t *adapter;
  unsigned callbacks;
  uintptr_t generation;
  struct k_sem idle;
} dma_bindings[DMA_BINDINGS];
static uintptr_t dma_next_generation = DMA_BINDINGS;

static void dma_wake(wl_kernel_uart_dma_t *adapter) {
  atomic_set(&adapter->service_pending, 1);
  k_sem_give(&adapter->wake);
  if (adapter->config.notify != NULL)
    adapter->config.notify(adapter->config.notify_user_data);
}

static void record_tx_completion(wl_kernel_uart_dma_t *adapter, atomic_val_t completion) {
  if (atomic_get(&adapter->tx_active) == 0 ||
      !atomic_cas(&adapter->tx_completion, TX_COMPLETION_NONE, completion)) {
    atomic_inc(&adapter->errors);
  }
}

static int service_tx_completion(wl_kernel_uart_dma_t *adapter, uint32_t now_ms) {
  k_spinlock_key_t tx_key = k_spin_lock(&adapter->tx_lock);
  atomic_val_t completion = atomic_set(&adapter->tx_completion, TX_COMPLETION_NONE);
  int ret = WL_OK;
  if (completion != TX_COMPLETION_NONE) {
    wl_packet_token_t token = adapter->tx_token;
    adapter->tx_data = NULL;
    adapter->tx_length = 0U;
    if (completion == TX_COMPLETION_DONE && adapter->config.wait_for_tx_idle &&
        !atomic_get(&adapter->stopping)) {
      adapter->drain_pending = 1U;
      adapter->drain_due = now_ms;
    } else {
      adapter->tx_token = (wl_packet_token_t){0};
      atomic_set(&adapter->tx_active, 0);
    }
    k_spin_unlock(&adapter->tx_lock, tx_key);
    ret = wl_packet_tx_publish(
        adapter->config.port, token,
        completion == TX_COMPLETION_DONE
            ? (adapter->config.wait_for_tx_idle ? WL_PACKET_CONSUMED : WL_PACKET_DRAINED)
            : WL_PACKET_TX_ERROR);
    /* A stop can retire memory without needing an unavailable physical idle. */
    if (completion == TX_COMPLETION_DONE && adapter->config.wait_for_tx_idle &&
        atomic_get(&adapter->stopping))
      (void)wl_packet_tx_publish(adapter->config.port, token, WL_PACKET_TX_ERROR);
  } else {
    k_spin_unlock(&adapter->tx_lock, tx_key);
  }
  if (adapter->drain_pending &&
      ((int32_t)(now_ms - adapter->drain_due) >= 0 || atomic_get(&adapter->stopping))) {
    int idle =
        atomic_get(&adapter->stopping) ? -ECANCELED : uart_irq_tx_complete(adapter->config.uart);
    if (idle == 0)
      adapter->drain_due =
          now_ms + (adapter->config.drain_check_ms ? adapter->config.drain_check_ms : 1U);
    else {
      wl_packet_token_t token = adapter->tx_token;
      adapter->drain_pending = 0U;
      adapter->tx_token = (wl_packet_token_t){0};
      atomic_set(&adapter->tx_active, 0);
      ret = wl_packet_tx_publish(adapter->config.port, token,
                                 idle > 0 ? WL_PACKET_DRAINED : WL_PACKET_TX_ERROR);
    }
  }
  if (ret != WL_OK)
    atomic_inc(&adapter->errors);
  return ret;
}

static void uart_dma_callback(const struct device *dev, struct uart_event *event, void *user_data) {
  wl_kernel_uart_dma_t *adapter = user_data;

  if (adapter == NULL || dev != adapter->config.uart) {
    return;
  }
  switch (event->type) {
  case UART_TX_DONE: {
    k_spinlock_key_t key = k_spin_lock(&adapter->tx_lock);
    atomic_inc(&adapter->tx_done_events);
    if (event->data.tx.buf != adapter->tx_data || event->data.tx.len != adapter->tx_length) {
      atomic_inc(&adapter->errors);
      k_spin_unlock(&adapter->tx_lock, key);
      break; /* A stale/mismatched report cannot terminate a new transfer. */
    } else {
      record_tx_completion(adapter, TX_COMPLETION_DONE);
    }
    k_spin_unlock(&adapter->tx_lock, key);
    break;
  }
  case UART_TX_ABORTED: {
    k_spinlock_key_t key = k_spin_lock(&adapter->tx_lock);
    atomic_inc(&adapter->tx_aborted_events);
    if (event->data.tx.buf != adapter->tx_data || event->data.tx.len > adapter->tx_length) {
      atomic_inc(&adapter->errors);
      k_spin_unlock(&adapter->tx_lock, key);
      break;
    }
    record_tx_completion(adapter, TX_COMPLETION_ABORTED);
    k_spin_unlock(&adapter->tx_lock, key);
    break;
  }
  case UART_RX_RDY: {
    wl_kernel_uart_dma_slot_t *slot;
    size_t end;
    size_t cache_offset;
    size_t cache_length;
    uint32_t started = 0U;
    k_spinlock_key_t key;
    int ret;

    atomic_inc(&adapter->rx_ready_events);
    if (adapter->config.cycle_counter != NULL) {
      started = adapter->config.cycle_counter(adapter->config.cycle_counter_user_data);
    }
    key = k_spin_lock(&adapter->rx_lock);
    slot = find_slot(adapter, event->data.rx.buf);
    if (slot == NULL || event->data.rx.offset > slot->buffer.length ||
        event->data.rx.len > slot->buffer.length - event->data.rx.offset) {
      k_spin_unlock(&adapter->rx_lock, key);
      mark_abort(adapter);
      break;
    }
    end = event->data.rx.offset + event->data.rx.len;
    if (event->data.rx.offset > slot->received) {
      k_spin_unlock(&adapter->rx_lock, key);
      mark_abort(adapter);
      break;
    }
    if (end > slot->received) {
      cache_offset = slot->received;
      cache_length = end - cache_offset;
      if (adapter->config.complete_from_dma != NULL) {
        adapter->config.complete_from_dma(adapter->config.cache_user_data,
                                          slot->buffer.data + cache_offset, cache_length);
      }
      slot->received = end;
      ret = drain_ready_slots_locked(adapter);
    } else {
      ret = WL_OK;
    }
    k_spin_unlock(&adapter->rx_lock, key);
    if (adapter->config.cycle_counter != NULL) {
      atomic_add(
          &adapter->producer_cycles,
          (atomic_val_t)(adapter->config.cycle_counter(adapter->config.cycle_counter_user_data) -
                         started));
    }
    if (ret != WL_OK && ret != WL_ERR_WOULD_BLOCK) {
      mark_abort(adapter);
      break;
    }
    (void)submit_requested_buffer(adapter);
    break;
  }
  case UART_RX_BUF_REQUEST:
    atomic_inc(&adapter->buffer_requests);
    if (atomic_get(&adapter->stopping) != 0) {
      atomic_set(&adapter->expected_disabled, 1);
      break;
    }
    {
      k_spinlock_key_t key = k_spin_lock(&adapter->rx_lock);

      /* A driver may issue a fresh request after promoting its look-ahead
       * buffer even when the previous request could not be answered. Both
       * events represent the same one-slot demand, so coalesce them. */
      adapter->buffer_request_pending = 1U;
      k_spin_unlock(&adapter->rx_lock, key);
    }
    if (submit_requested_buffer(adapter) != WL_OK) {
      atomic_set(&adapter->expected_disabled, 1);
      atomic_set(&adapter->paused, 1);
    }
    break;
  case UART_RX_BUF_RELEASED: {
    k_spinlock_key_t key = k_spin_lock(&adapter->rx_lock);
    wl_kernel_uart_dma_slot_t *slot = find_slot(adapter, event->data.rx_buf.buf);
    if (slot == NULL) {
      k_spin_unlock(&adapter->rx_lock, key);
      mark_abort(adapter);
      break;
    }
    slot->driver_owned = 0U;
    slot->released = 1U;
    int ret = drain_ready_slots_locked(adapter);
    k_spin_unlock(&adapter->rx_lock, key);
    if (ret != WL_OK && ret != WL_ERR_WOULD_BLOCK) {
      mark_abort(adapter);
      break;
    }
    (void)submit_requested_buffer(adapter);
    break;
  }
  case UART_RX_STOPPED:
    if (atomic_get(&adapter->stopping) == 0) {
      mark_abort(adapter);
    }
    break;
  case UART_RX_DISABLED: {
    k_spinlock_key_t key = k_spin_lock(&adapter->rx_lock);

    /* uart_rx_buf_rsp() can only answer a request while RX is enabled. */
    adapter->buffer_request_pending = 0U;
    k_spin_unlock(&adapter->rx_lock, key);
  }
    if (atomic_get(&adapter->stopping) != 0) {
      atomic_set(&adapter->expected_disabled, 0);
    } else if (atomic_get(&adapter->abort_pending) == 0 &&
               (atomic_get(&adapter->paused) == 0 ||
                !atomic_cas(&adapter->expected_disabled, 1, 0))) {
      mark_abort(adapter);
    } else if (atomic_get(&adapter->paused) != 0) {
      atomic_set(&adapter->gap_pending, 1);
    }
    /* Publish this last: service may touch slot progress once running is zero. */
    atomic_set(&adapter->running, 0);
    break;
  default:
    break;
  }
}

static void uart_dma_dispatch(const struct device *dev, struct uart_event *event, void *context) {
  const uintptr_t generation = (uintptr_t)context;
  const size_t index = generation & (DMA_BINDINGS - 1U);
  k_spinlock_key_t key = k_spin_lock(&dma_binding_lock);
  wl_kernel_uart_dma_t *adapter = dma_bindings[index].adapter;
  if (dma_bindings[index].generation != generation || adapter == NULL) {
    k_spin_unlock(&dma_binding_lock, key);
    return;
  }
  ++dma_bindings[index].callbacks;
  k_spin_unlock(&dma_binding_lock, key);
  uart_dma_callback(dev, event, adapter);
  dma_wake(adapter); /* Every state/data publication wakes the single owner. */
  key = k_spin_lock(&dma_binding_lock);
  if (--dma_bindings[index].callbacks == 0U)
    k_sem_give(&dma_bindings[index].idle);
  k_spin_unlock(&dma_binding_lock, key);
}

int wl_kernel_uart_dma_init(wl_kernel_uart_dma_t *adapter,
                            const wl_kernel_uart_dma_config_t *config) {
  int ret;

  if (adapter == NULL || config == NULL || config->uart == NULL || config->port == NULL ||
      config->fifo == NULL || config->stream == NULL || config->rx_buffers[0].data == NULL ||
      config->rx_buffers[0].length == 0U || config->rx_buffers[1].data == NULL ||
      config->rx_buffers[1].length == 0U ||
      spans_overlap(&config->rx_buffers[0], &config->rx_buffers[1]) ||
      (config->timeout_us < 0 && config->timeout_us != SYS_FOREVER_US) ||
      (config->tx_timeout_us < 0 && config->tx_timeout_us != SYS_FOREVER_US) ||
      config->drain_check_ms > INT32_MAX || config->tx_retry_ms > INT32_MAX ||
      !device_is_ready(config->uart)) {
    return WL_ERR_INVALID_ARG;
  }
  k_spinlock_key_t bind_key = k_spin_lock(&dma_binding_lock);
  size_t binding = DMA_BINDINGS;
  for (size_t i = 0U; i < DMA_BINDINGS; ++i) {
    if (dma_bindings[i].adapter != NULL && dma_bindings[i].adapter->config.uart == config->uart) {
      k_spin_unlock(&dma_binding_lock, bind_key);
      return WL_ERR_BUSY;
    }
    if (dma_bindings[i].adapter == NULL && dma_bindings[i].callbacks == 0U)
      binding = i;
  }
  if (binding == DMA_BINDINGS || dma_next_generation > UINTPTR_MAX - DMA_BINDINGS) {
    k_spin_unlock(&dma_binding_lock, bind_key);
    return WL_ERR_NO_SPACE;
  }
  memset(adapter, 0, sizeof(*adapter));
  adapter->binding = (uint8_t)binding;
  k_sem_init(&adapter->wake, 0, 1);
  k_sem_init(&dma_bindings[binding].idle, 0, 1);
  adapter->config = *config;
  adapter->slots[0].buffer = config->rx_buffers[0];
  adapter->slots[1].buffer = config->rx_buffers[1];
  dma_bindings[binding].adapter = adapter;
  dma_bindings[binding].generation = dma_next_generation | binding;
  dma_next_generation += DMA_BINDINGS;
  k_spin_unlock(&dma_binding_lock, bind_key);
  ret =
      uart_callback_set(config->uart, uart_dma_dispatch, (void *)dma_bindings[binding].generation);
  if (ret != 0) {
    bind_key = k_spin_lock(&dma_binding_lock);
    dma_bindings[binding].adapter = NULL;
    adapter->detached = 1U;
    unsigned pending = dma_bindings[binding].callbacks;
    k_spin_unlock(&dma_binding_lock, bind_key);
    while (pending != 0U) {
      (void)k_sem_take(&dma_bindings[binding].idle, K_FOREVER);
      bind_key = k_spin_lock(&dma_binding_lock);
      pending = dma_bindings[binding].callbacks;
      k_spin_unlock(&dma_binding_lock, bind_key);
    }
    return WL_ERR_NOT_SUPPORTED;
  }
  return WL_OK;
}

static int start_rx(wl_kernel_uart_dma_t *adapter) {
  wl_kernel_uart_dma_slot_t *slot;
  k_spinlock_key_t key;
  int ret;

  if (atomic_get(&adapter->running) != 0 || any_slot_is_active(adapter)) {
    return WL_ERR_BUSY;
  }
  /* uart_rx_enable() may synchronously request its look-ahead buffer. */
  atomic_set(&adapter->paused, 0);
  atomic_set(&adapter->expected_disabled, 0);
  atomic_set(&adapter->gap_pending, 0);
  key = k_spin_lock(&adapter->rx_lock);
  slot = arm_free_slot_locked(adapter);
  k_spin_unlock(&adapter->rx_lock, key);
  if (slot == NULL) {
    return WL_ERR_BUSY;
  }
  if (adapter->config.prepare_for_dma != NULL) {
    adapter->config.prepare_for_dma(adapter->config.cache_user_data, slot->buffer.data,
                                    slot->buffer.length);
  }
  atomic_set(&adapter->running, 1);
  ret = uart_rx_enable(adapter->config.uart, slot->buffer.data, slot->buffer.length,
                       adapter->config.timeout_us);
  if (ret != 0) {
    atomic_set(&adapter->running, 0);
    atomic_set(&adapter->paused, 1);
    key = k_spin_lock(&adapter->rx_lock);
    for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
      reset_slot(&adapter->slots[i]);
    }
    adapter->buffer_request_pending = 0U;
    k_spin_unlock(&adapter->rx_lock, key);
    return WL_ERR_IO;
  }
  return WL_OK;
}

int wl_kernel_uart_dma_start(wl_kernel_uart_dma_t *adapter) {
  int ret;

  if (adapter == NULL || adapter->config.port == NULL || adapter->detached) {
    return WL_ERR_INVALID_ARG;
  }
  if (atomic_get(&adapter->started) != 0 || atomic_get(&adapter->stopping) != 0) {
    return WL_ERR_BUSY;
  }
  atomic_set(&adapter->started, 1);
  ret = start_rx(adapter);
  if (ret != WL_OK) {
    atomic_set(&adapter->started, 0);
  }
  return ret;
}

int wl_kernel_uart_dma_stop(wl_kernel_uart_dma_t *adapter) {
  int ret;

  if (adapter == NULL || adapter->config.port == NULL || adapter->detached) {
    return WL_ERR_INVALID_ARG;
  }
  if (atomic_get(&adapter->started) == 0 && atomic_get(&adapter->stopping) == 0) {
    return WL_OK;
  }

  atomic_set(&adapter->stopping, 1);
  atomic_set(&adapter->paused, 0);
  atomic_set(&adapter->expected_disabled, 1);
  {
    k_spinlock_key_t key = k_spin_lock(&adapter->rx_lock);

    adapter->buffer_request_pending = 0U;
    k_spin_unlock(&adapter->rx_lock, key);
  }
  if (atomic_get(&adapter->running) != 0) {
    ret = uart_rx_disable(adapter->config.uart);
    if (ret == -EFAULT) {
      atomic_set(&adapter->running, 0);
      /* Disabled is not a buffer-return event. A queued callback may still
       * owe RX_BUF_RELEASED even though another disable returns EFAULT. */
    } else if (ret != 0) {
      atomic_inc(&adapter->errors);
      return WL_ERR_IO;
    }
  }

  if (atomic_get(&adapter->tx_active) != 0 &&
      atomic_get(&adapter->tx_completion) == TX_COMPLETION_NONE) {
    ret = uart_tx_abort(adapter->config.uart);
    if (ret == -EFAULT) {
      record_tx_completion(adapter, TX_COMPLETION_ABORTED);
    } else if (ret != 0) {
      atomic_inc(&adapter->errors);
      return WL_ERR_IO;
    }
  }
  dma_wake(adapter);
  return WL_OK;
}

int wl_kernel_uart_dma_service(wl_kernel_uart_dma_t *adapter, uint32_t now_ms) {
  k_spinlock_key_t key;
  int ret;

  if (adapter == NULL || adapter->config.port == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  (void)k_sem_take(&adapter->wake, K_NO_WAIT);
  atomic_set(&adapter->service_pending, 0);
  if (adapter->tx_retry_pending && (int32_t)(now_ms - adapter->tx_retry_due) >= 0) {
    adapter->tx_retry_pending = 0U;
    wl_packet_activity(adapter->config.port);
  }
  ret = service_tx_completion(adapter, now_ms);
  if (ret != WL_OK) {
    return ret;
  }
  if (atomic_get(&adapter->stopping) != 0) {
    if (atomic_get(&adapter->running) != 0 || atomic_get(&adapter->tx_active) != 0 ||
        driver_owns_any_slot(adapter)) {
      return WL_ERR_WOULD_BLOCK;
    }
    key = k_spin_lock(&adapter->rx_lock);
    for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
      reset_slot(&adapter->slots[i]);
    }
    adapter->buffer_request_pending = 0U;
    k_spin_unlock(&adapter->rx_lock, key);
    atomic_set(&adapter->abort_pending, 0);
    atomic_set(&adapter->abort_disable_requested, 0);
    atomic_set(&adapter->recovery_barrier, 0);
    atomic_set(&adapter->gap_pending, 0);
    adapter->tx_retry_pending = 0U;
    atomic_set(&adapter->started, 0);
    atomic_set(&adapter->stopping, 0);
    atomic_set(&adapter->paused, 0);
    atomic_set(&adapter->expected_disabled, 0);
    return WL_OK;
  }
  if (atomic_get(&adapter->abort_pending) != 0) {
    if (atomic_get(&adapter->running) != 0) {
      if (atomic_cas(&adapter->abort_disable_requested, 0, 1)) {
        ret = uart_rx_disable(adapter->config.uart);
        if (ret == -EFAULT) {
          atomic_set(&adapter->running, 0);
        } else if (ret != 0) {
          atomic_inc(&adapter->errors);
          return WL_ERR_IO;
        }
      }
      return WL_ERR_WOULD_BLOCK;
    }
    if (driver_owns_any_slot(adapter)) {
      return WL_ERR_WOULD_BLOCK;
    }
    key = k_spin_lock(&adapter->rx_lock);
    for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
      reset_slot(&adapter->slots[i]);
    }
    adapter->buffer_request_pending = 0U;
    k_spin_unlock(&adapter->rx_lock, key);
    wl_byte_fifo_note_gap(adapter->config.fifo);
    atomic_set(&adapter->abort_pending, 0);
    atomic_set(&adapter->abort_disable_requested, 0);
    atomic_set(&adapter->expected_disabled, 0);
    atomic_set(&adapter->paused, 1);
    atomic_set(&adapter->recovery_barrier, 1);
    atomic_set(&adapter->service_pending, 1);
    return WL_ERR_WOULD_BLOCK;
  }
  int input = wl_byte_fifo_to_cobs(adapter->config.fifo, adapter->config.stream, 16U);
  if (input != WL_OK && input != WL_ERR_WOULD_BLOCK)
    return input;
  key = k_spin_lock(&adapter->rx_lock);
  ret = drain_ready_slots_locked(adapter);
  k_spin_unlock(&adapter->rx_lock, key);
  if (ret != WL_OK && ret != WL_ERR_WOULD_BLOCK) {
    mark_abort(adapter);
    return WL_ERR_INVALID_STATE;
  }
  if (ret == WL_OK && atomic_get(&adapter->running) != 0) {
    ret = submit_requested_buffer(adapter);
    if (ret != WL_OK && ret != WL_ERR_WOULD_BLOCK) {
      return ret;
    }
  }
  if (atomic_get(&adapter->recovery_barrier) != 0) {
    atomic_set(&adapter->recovery_barrier, 0);
    return WL_ERR_WOULD_BLOCK;
  }
  if (atomic_get(&adapter->started) != 0 && atomic_get(&adapter->running) == 0 &&
      atomic_get(&adapter->paused) != 0) {
    if (any_slot_is_active(adapter)) {
      return WL_ERR_WOULD_BLOCK;
    }
    if (atomic_set(&adapter->gap_pending, 0) != 0) {
      wl_byte_fifo_note_gap(adapter->config.fifo);
      atomic_set(&adapter->recovery_barrier, 1);
      atomic_set(&adapter->service_pending, 1);
      return WL_ERR_WOULD_BLOCK;
    }
    return start_rx(adapter);
  }
  return WL_OK;
}

wl_io_readiness_t wl_kernel_uart_dma_readiness(const void *context, uint32_t now_ms) {
  const wl_kernel_uart_dma_t *adapter = context;
  wl_io_readiness_t value = wl_io_readiness_idle();
  value.runnable = wl_byte_fifo_cobs_ready(adapter->config.fifo, adapter->config.stream) ||
                   atomic_get(&adapter->service_pending) ||
                   atomic_get(&adapter->tx_completion) != TX_COMPLETION_NONE;
  value.blocked = atomic_get(&adapter->tx_active) || atomic_get(&adapter->paused) ||
                  atomic_get(&adapter->stopping) || atomic_get(&adapter->abort_pending);
  if (adapter->tx_retry_pending)
    wl_io_readiness_merge(&value, wl_io_readiness_at((int32_t)(now_ms - adapter->tx_retry_due) >= 0
                                                         ? 0U
                                                         : adapter->tx_retry_due - now_ms));
  if (adapter->drain_pending)
    wl_io_readiness_merge(&value, wl_io_readiness_at((int32_t)(now_ms - adapter->drain_due) >= 0
                                                         ? 0U
                                                         : adapter->drain_due - now_ms));
  wl_kernel_uart_dma_t *owner = (wl_kernel_uart_dma_t *)(void *)adapter;
  k_spinlock_key_t key = k_spin_lock(&owner->rx_lock);
  bool waiting_bytes = false, slots_active = false, slots_owned = false;
  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    waiting_bytes |= owner->slots[i].received > owner->slots[i].forwarded;
    slots_active |= owner->slots[i].order != 0U;
    slots_owned |= owner->slots[i].driver_owned != 0U;
  }
  if (atomic_get(&adapter->stopping)) {
    value.runnable |=
        !atomic_get(&adapter->running) && !atomic_get(&adapter->tx_active) && !slots_owned;
  } else if (atomic_get(&adapter->abort_pending)) {
    value.runnable |= !atomic_get(&adapter->running) && !slots_owned;
  } else if (wl_byte_fifo_has_space(adapter->config.fifo) &&
             (waiting_bytes || (atomic_get(&adapter->started) && atomic_get(&adapter->paused) &&
                                !atomic_get(&adapter->running) && !slots_active)))
    value.runnable = 1U;
  k_spin_unlock(&owner->rx_lock, key);
  return value;
}

static wl_err_t dma_wait_activity(wl_kernel_uart_dma_t *adapter, k_timeout_t timeout) {
  return k_sem_take(&adapter->wake, timeout) == 0 ? WL_OK : WL_ERR_NO_DATA;
}
wl_err_t wl_kernel_uart_dma_wait(wl_kernel_uart_dma_t *adapter, k_timeout_t timeout) {
  if (adapter == NULL)
    return WL_ERR_INVALID_ARG;
  if (!atomic_get(&adapter->started) || atomic_get(&adapter->stopping))
    return WL_ERR_CANCELLED;
  wl_err_t result = dma_wait_activity(adapter, timeout);
  if (!atomic_get(&adapter->started) || atomic_get(&adapter->stopping))
    return WL_ERR_CANCELLED;
  return result;
}
void wl_kernel_uart_dma_notify(void *context) {
  k_sem_give(&((wl_kernel_uart_dma_t *)context)->wake);
}
void wl_kernel_uart_dma_quiesce(wl_kernel_uart_dma_t *adapter) {
  if (adapter == NULL || adapter->config.port == NULL || adapter->detached)
    return;
  wl_packet_stop(adapter->config.port);
  (void)wl_kernel_uart_dma_stop(adapter);
  while (atomic_get(&adapter->started) || atomic_get(&adapter->stopping)) {
    uint32_t now = k_uptime_get_32();
    (void)wl_kernel_uart_dma_service(adapter, now);
    if (!atomic_get(&adapter->started) && !atomic_get(&adapter->stopping))
      break;
    wl_io_readiness_t ready = wl_kernel_uart_dma_readiness(adapter, now);
    if (!ready.runnable)
      (void)dma_wait_activity(adapter, wl_kernel_timeout_ms(ready.next_deadline_ms));
  }
  (void)uart_callback_set(adapter->config.uart, NULL, NULL);
  const size_t binding = adapter->binding;
  k_spinlock_key_t key = k_spin_lock(&dma_binding_lock);
  dma_bindings[binding].adapter = NULL;
  unsigned pending = dma_bindings[binding].callbacks;
  k_spin_unlock(&dma_binding_lock, key);
  while (pending != 0U) {
    (void)k_sem_take(&dma_bindings[binding].idle, K_FOREVER);
    key = k_spin_lock(&dma_binding_lock);
    pending = dma_bindings[binding].callbacks;
    k_spin_unlock(&dma_binding_lock, key);
  }
  (void)wl_packet_stream_abort_rx(adapter->config.stream);
  adapter->config.notify = NULL;
  adapter->detached = 1U;
}

void wl_kernel_uart_dma_reset_stats(wl_kernel_uart_dma_t *adapter) {
  if (adapter == NULL) {
    return;
  }
  atomic_set(&adapter->buffer_requests, 0);
  atomic_set(&adapter->rx_ready_events, 0);
  atomic_set(&adapter->published_bytes, 0);
  atomic_set(&adapter->producer_cycles, 0);
  atomic_set(&adapter->tx_submissions, 0);
  atomic_set(&adapter->tx_done_events, 0);
  atomic_set(&adapter->tx_aborted_events, 0);
  atomic_set(&adapter->tx_busy, 0);
  atomic_set(&adapter->errors, 0);
}

void wl_kernel_uart_dma_get_stats(const wl_kernel_uart_dma_t *adapter,
                                  wl_kernel_uart_dma_stats_t *out_stats) {
  if (adapter == NULL || out_stats == NULL) {
    return;
  }
  *out_stats = (wl_kernel_uart_dma_stats_t){
      .buffer_requests = (uint32_t)atomic_get(&adapter->buffer_requests),
      .rx_ready_events = (uint32_t)atomic_get(&adapter->rx_ready_events),
      .published_bytes = (uint32_t)atomic_get(&adapter->published_bytes),
      .producer_cycles = (uint32_t)atomic_get(&adapter->producer_cycles),
      .tx_submissions = (uint32_t)atomic_get(&adapter->tx_submissions),
      .tx_done_events = (uint32_t)atomic_get(&adapter->tx_done_events),
      .tx_aborted_events = (uint32_t)atomic_get(&adapter->tx_aborted_events),
      .tx_busy = (uint32_t)atomic_get(&adapter->tx_busy),
      .errors = (uint32_t)atomic_get(&adapter->errors),
      .started = atomic_get(&adapter->started) != 0 ? 1U : 0U,
      .stopping = atomic_get(&adapter->stopping) != 0 ? 1U : 0U,
      .running = atomic_get(&adapter->running) != 0 ? 1U : 0U,
      .paused = atomic_get(&adapter->paused) != 0 ? 1U : 0U,
      .tx_active = atomic_get(&adapter->tx_active) != 0 ? 1U : 0U,
  };
}
