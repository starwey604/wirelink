/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/zephyr/uart_dma.h"

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

  return left_address <= right_address
             ? right_address - left_address < left->length
             : left_address - right_address < right->length;
}

static void reset_slot(wl_zephyr_uart_dma_slot_t *slot) {
  slot->received = 0U;
  slot->forwarded = 0U;
  slot->order = 0U;
  slot->driver_owned = 0U;
  slot->released = 0U;
}

static wl_zephyr_uart_dma_slot_t *find_slot(wl_zephyr_uart_dma_t *adapter,
                                            uint8_t *data) {
  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    wl_zephyr_uart_dma_slot_t *slot = &adapter->slots[i];

    if (slot->order != 0U && slot->buffer.data == data) {
      return slot;
    }
  }
  return NULL;
}

static wl_zephyr_uart_dma_slot_t *
find_free_slot(wl_zephyr_uart_dma_t *adapter) {
  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    if (adapter->slots[i].order == 0U) {
      return &adapter->slots[i];
    }
  }
  return NULL;
}

static bool driver_owns_any_slot(const wl_zephyr_uart_dma_t *adapter) {
  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    if (adapter->slots[i].driver_owned != 0U) {
      return true;
    }
  }
  return false;
}

static bool any_slot_is_active(const wl_zephyr_uart_dma_t *adapter) {
  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    if (adapter->slots[i].order != 0U) {
      return true;
    }
  }
  return false;
}

static void mark_abort(wl_zephyr_uart_dma_t *adapter) {
  atomic_set(&adapter->abort_pending, 1);
  atomic_inc(&adapter->errors);
}

static wl_zephyr_uart_dma_slot_t *oldest_pending_slot(
    wl_zephyr_uart_dma_t *adapter) {
  wl_zephyr_uart_dma_slot_t *oldest = NULL;

  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    wl_zephyr_uart_dma_slot_t *slot = &adapter->slots[i];

    if (slot->order != 0U && slot->forwarded < slot->received &&
        (oldest == NULL || slot->order < oldest->order)) {
      oldest = slot;
    }
  }
  return oldest;
}

static void retire_forwarded_slots(wl_zephyr_uart_dma_t *adapter) {
  for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
    wl_zephyr_uart_dma_slot_t *slot = &adapter->slots[i];

    if (slot->order != 0U && slot->released != 0U &&
        slot->forwarded == slot->received) {
      reset_slot(slot);
    }
  }
}

/* rx_lock makes callback and owner service one logical SPSC producer. Bytes
 * remain in a released staging slot when the ring is full; that is
 * backpressure, not an overflow, until the UART actually has to stop. */
static int drain_ready_slots_locked(wl_zephyr_uart_dma_t *adapter) {
  for (;;) {
    wl_zephyr_uart_dma_slot_t *slot;
    wl_span_t span = {0};
    size_t length;
    int ret;

    retire_forwarded_slots(adapter);
    slot = oldest_pending_slot(adapter);
    if (slot == NULL) {
      return WL_OK;
    }
    ret = wl_rx_reserve(adapter->config.link, &span);
    if (ret != WL_OK) {
      return ret;
    }
    if (span.length == 0U) {
      (void)wl_rx_commit(adapter->config.link, 0U);
      return WL_ERR_WOULD_BLOCK;
    }
    length = slot->received - slot->forwarded;
    if (length > span.length) {
      length = span.length;
    }
    memcpy(span.data, slot->buffer.data + slot->forwarded, length);
    ret = wl_rx_commit(adapter->config.link, length);
    if (ret != WL_OK) {
      return ret;
    }
    slot->forwarded += length;
    atomic_add(&adapter->published_bytes, (atomic_val_t)length);
  }
}

static wl_zephyr_uart_dma_slot_t *arm_free_slot_locked(
    wl_zephyr_uart_dma_t *adapter) {
  wl_zephyr_uart_dma_slot_t *slot;

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

static int submit_requested_buffer(wl_zephyr_uart_dma_t *adapter) {
  wl_zephyr_uart_dma_slot_t *slot;
  k_spinlock_key_t key;
  int ret;

  key = k_spin_lock(&adapter->rx_lock);
  if (adapter->buffer_request_pending == 0U ||
      atomic_get(&adapter->stopping) != 0 ||
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
    adapter->config.prepare_for_dma(adapter->config.cache_user_data,
                                    slot->buffer.data, slot->buffer.length);
  }
  ret = uart_rx_buf_rsp(adapter->config.uart, slot->buffer.data,
                        slot->buffer.length);
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

static wl_sink_result_t uart_dma_sink(void *user_data, wl_io_token_t token,
                                      const uint8_t *data, size_t length) {
  wl_zephyr_uart_dma_t *adapter = user_data;
  int ret;

  if (adapter == NULL || data == NULL || length == 0U ||
      atomic_get(&adapter->started) == 0 ||
      atomic_get(&adapter->stopping) != 0) {
    return WL_SINK_FAILED;
  }
  if (!atomic_cas(&adapter->tx_active, 0, 1)) {
    atomic_inc(&adapter->tx_busy);
    return WL_SINK_BUSY;
  }
  adapter->tx_token = token;
  adapter->tx_data = data;
  adapter->tx_length = length;
  atomic_set(&adapter->tx_completion, TX_COMPLETION_NONE);

  ret = uart_tx(adapter->config.uart, data, length,
                adapter->config.tx_timeout_us);
  if (ret == 0) {
    atomic_inc(&adapter->tx_submissions);
    return WL_SINK_STARTED;
  }

  adapter->tx_token = 0U;
  adapter->tx_data = NULL;
  adapter->tx_length = 0U;
  atomic_set(&adapter->tx_active, 0);
  if (ret == -EBUSY || ret == -EAGAIN) {
    atomic_inc(&adapter->tx_busy);
    return WL_SINK_BUSY;
  }
  atomic_inc(&adapter->errors);
  return WL_SINK_FAILED;
}

static void record_tx_completion(wl_zephyr_uart_dma_t *adapter,
                                 atomic_val_t completion) {
  if (atomic_get(&adapter->tx_active) == 0 ||
      !atomic_cas(&adapter->tx_completion, TX_COMPLETION_NONE, completion)) {
    atomic_inc(&adapter->errors);
  }
}

static int service_tx_completion(wl_zephyr_uart_dma_t *adapter) {
  atomic_val_t completion = atomic_get(&adapter->tx_completion);
  wl_io_token_t token;
  int ret;

  if (completion == TX_COMPLETION_NONE) {
    return WL_OK;
  }
  if (completion == TX_COMPLETION_DONE && adapter->config.wait_for_tx_idle) {
    /* Some DMA-backed drivers raise UART_TX_DONE when DMA has filled the UART
     * FIFO, before the final byte has left the peripheral. Where the optional
     * IRQ query exists, keep the core's buffer/transaction ownership until
     * the UART reports physically idle. */
    ret = uart_irq_tx_complete(adapter->config.uart);
    if (ret == 0) {
      return WL_ERR_WOULD_BLOCK;
    }
  }
  if (!atomic_cas(&adapter->tx_completion, completion, TX_COMPLETION_NONE)) {
    return WL_OK;
  }

  token = adapter->tx_token;
  adapter->tx_token = 0U;
  adapter->tx_data = NULL;
  adapter->tx_length = 0U;
  /* wl_tx_complete() may synchronously submit a retry or pending ACK. */
  atomic_set(&adapter->tx_active, 0);
  ret = wl_tx_complete(adapter->config.link, token,
                       completion == TX_COMPLETION_DONE ? WL_OK : WL_ERR_IO);
  if (ret != WL_OK) {
    atomic_inc(&adapter->errors);
  }
  return ret;
}

static void uart_dma_callback(const struct device *dev,
                              struct uart_event *event, void *user_data) {
  wl_zephyr_uart_dma_t *adapter = user_data;

  if (adapter == NULL || dev != adapter->config.uart) {
    return;
  }
  switch (event->type) {
  case UART_TX_DONE:
    atomic_inc(&adapter->tx_done_events);
    if (event->data.tx.buf != adapter->tx_data ||
        event->data.tx.len != adapter->tx_length) {
      atomic_inc(&adapter->errors);
      record_tx_completion(adapter, TX_COMPLETION_ABORTED);
    } else {
      record_tx_completion(adapter, TX_COMPLETION_DONE);
    }
    break;
  case UART_TX_ABORTED:
    atomic_inc(&adapter->tx_aborted_events);
    if (event->data.tx.buf != adapter->tx_data ||
        event->data.tx.len > adapter->tx_length) {
      atomic_inc(&adapter->errors);
    }
    record_tx_completion(adapter, TX_COMPLETION_ABORTED);
    break;
  case UART_RX_RDY: {
    wl_zephyr_uart_dma_slot_t *slot = find_slot(adapter, event->data.rx.buf);
    size_t end;
    size_t cache_offset;
    size_t cache_length;
    uint32_t started = 0U;
    k_spinlock_key_t key;
    int ret;

    atomic_inc(&adapter->rx_ready_events);
    if (adapter->config.cycle_counter != NULL) {
      started =
          adapter->config.cycle_counter(adapter->config.cycle_counter_user_data);
    }
    if (slot == NULL || event->data.rx.offset > slot->buffer.length ||
        event->data.rx.len > slot->buffer.length - event->data.rx.offset) {
      mark_abort(adapter);
      break;
    }
    end = event->data.rx.offset + event->data.rx.len;
    key = k_spin_lock(&adapter->rx_lock);
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
                                          slot->buffer.data + cache_offset,
                                          cache_length);
      }
      slot->received = end;
      ret = drain_ready_slots_locked(adapter);
    } else {
      ret = WL_OK;
    }
    k_spin_unlock(&adapter->rx_lock, key);
    if (adapter->config.cycle_counter != NULL) {
      atomic_add(&adapter->producer_cycles,
                 (atomic_val_t)(adapter->config.cycle_counter(
                                    adapter->config.cycle_counter_user_data) -
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

      if (adapter->buffer_request_pending != 0U) {
        k_spin_unlock(&adapter->rx_lock, key);
        mark_abort(adapter);
        break;
      }
      adapter->buffer_request_pending = 1U;
      k_spin_unlock(&adapter->rx_lock, key);
    }
    if (submit_requested_buffer(adapter) != WL_OK) {
      atomic_set(&adapter->expected_disabled, 1);
      atomic_set(&adapter->paused, 1);
    }
    break;
  case UART_RX_BUF_RELEASED: {
    wl_zephyr_uart_dma_slot_t *slot =
        find_slot(adapter, event->data.rx_buf.buf);

    if (slot == NULL) {
      mark_abort(adapter);
      break;
    }
    {
      k_spinlock_key_t key = k_spin_lock(&adapter->rx_lock);
      int ret;

      slot->driver_owned = 0U;
      slot->released = 1U;
      ret = drain_ready_slots_locked(adapter);
      k_spin_unlock(&adapter->rx_lock, key);
      if (ret != WL_OK && ret != WL_ERR_WOULD_BLOCK) {
        mark_abort(adapter);
        break;
      }
    }
    (void)submit_requested_buffer(adapter);
    break;
  }
  case UART_RX_STOPPED:
    if (atomic_get(&adapter->stopping) == 0) {
      mark_abort(adapter);
    }
    break;
  case UART_RX_DISABLED:
    if (atomic_get(&adapter->stopping) != 0) {
      atomic_set(&adapter->expected_disabled, 0);
    } else if (atomic_get(&adapter->abort_pending) == 0 &&
               (atomic_get(&adapter->paused) == 0 ||
                !atomic_cas(&adapter->expected_disabled, 1, 0))) {
      mark_abort(adapter);
    } else if (atomic_get(&adapter->paused) != 0) {
      adapter->gap_pending = 1U;
    }
    /* Publish this last: service may touch slot progress once running is zero. */
    atomic_set(&adapter->running, 0);
    break;
  default:
    break;
  }
}

int wl_zephyr_uart_dma_init(wl_zephyr_uart_dma_t *adapter,
                            const wl_zephyr_uart_dma_config_t *config) {
  int ret;

  if (adapter == NULL || config == NULL || config->uart == NULL ||
      config->link == NULL || config->rx_buffers[0].data == NULL ||
      config->rx_buffers[0].length == 0U ||
      config->rx_buffers[1].data == NULL ||
      config->rx_buffers[1].length == 0U ||
      spans_overlap(&config->rx_buffers[0], &config->rx_buffers[1]) ||
      (config->timeout_us < 0 && config->timeout_us != SYS_FOREVER_US) ||
      (config->tx_timeout_us < 0 && config->tx_timeout_us != SYS_FOREVER_US) ||
      !device_is_ready(config->uart)) {
    return WL_ERR_INVALID_ARG;
  }
  memset(adapter, 0, sizeof(*adapter));
  adapter->config = *config;
  adapter->slots[0].buffer = config->rx_buffers[0];
  adapter->slots[1].buffer = config->rx_buffers[1];
  ret = uart_callback_set(config->uart, uart_dma_callback, adapter);
  if (ret != 0) {
    return WL_ERR_NOT_SUPPORTED;
  }
  return wl_set_sink(config->link, uart_dma_sink, adapter);
}

static int start_rx(wl_zephyr_uart_dma_t *adapter) {
  wl_zephyr_uart_dma_slot_t *slot;
  k_spinlock_key_t key;
  int ret;

  if (atomic_get(&adapter->running) != 0 || any_slot_is_active(adapter)) {
    return WL_ERR_BUSY;
  }
  /* uart_rx_enable() may synchronously request its look-ahead buffer. */
  atomic_set(&adapter->paused, 0);
  atomic_set(&adapter->expected_disabled, 0);
  adapter->gap_pending = 0U;
  key = k_spin_lock(&adapter->rx_lock);
  slot = arm_free_slot_locked(adapter);
  k_spin_unlock(&adapter->rx_lock, key);
  if (slot == NULL) {
    return WL_ERR_BUSY;
  }
  if (adapter->config.prepare_for_dma != NULL) {
    adapter->config.prepare_for_dma(adapter->config.cache_user_data,
                                    slot->buffer.data, slot->buffer.length);
  }
  atomic_set(&adapter->running, 1);
  ret = uart_rx_enable(adapter->config.uart, slot->buffer.data,
                       slot->buffer.length, adapter->config.timeout_us);
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

int wl_zephyr_uart_dma_start(wl_zephyr_uart_dma_t *adapter) {
  int ret;

  if (adapter == NULL || adapter->config.link == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (atomic_get(&adapter->started) != 0 ||
      atomic_get(&adapter->stopping) != 0) {
    return WL_ERR_BUSY;
  }
  atomic_set(&adapter->started, 1);
  ret = start_rx(adapter);
  if (ret != WL_OK) {
    atomic_set(&adapter->started, 0);
  }
  return ret;
}

int wl_zephyr_uart_dma_stop(wl_zephyr_uart_dma_t *adapter) {
  int ret;

  if (adapter == NULL || adapter->config.link == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (atomic_get(&adapter->started) == 0 &&
      atomic_get(&adapter->stopping) == 0) {
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
      k_spinlock_key_t key;

      atomic_set(&adapter->running, 0);
      key = k_spin_lock(&adapter->rx_lock);
      for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
        if (adapter->slots[i].order != 0U) {
          adapter->slots[i].driver_owned = 0U;
          adapter->slots[i].released = 1U;
        }
      }
      k_spin_unlock(&adapter->rx_lock, key);
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
  return WL_OK;
}

int wl_zephyr_uart_dma_service(wl_zephyr_uart_dma_t *adapter) {
  k_spinlock_key_t key;
  int ret;

  if (adapter == NULL || adapter->config.link == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  ret = service_tx_completion(adapter);
  if (ret != WL_OK) {
    return ret;
  }
  if (atomic_get(&adapter->abort_pending) != 0) {
    if (atomic_get(&adapter->running) != 0) {
      if (atomic_cas(&adapter->abort_disable_requested, 0, 1)) {
        ret = uart_rx_disable(adapter->config.uart);
        if (ret == -EFAULT) {
          atomic_set(&adapter->running, 0);
          key = k_spin_lock(&adapter->rx_lock);
          for (size_t i = 0U; i < RX_SLOT_COUNT; ++i) {
            if (adapter->slots[i].order != 0U) {
              adapter->slots[i].driver_owned = 0U;
              adapter->slots[i].released = 1U;
            }
          }
          k_spin_unlock(&adapter->rx_lock, key);
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
    wl_rx_note_overflow(adapter->config.link);
    atomic_set(&adapter->abort_pending, 0);
    atomic_set(&adapter->abort_disable_requested, 0);
    atomic_set(&adapter->expected_disabled, 0);
    atomic_set(&adapter->paused, 1);
    atomic_set(&adapter->recovery_barrier, 1);
    return WL_ERR_WOULD_BLOCK;
  }
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
  if (atomic_get(&adapter->stopping) != 0) {
    if (atomic_get(&adapter->running) != 0 ||
        atomic_get(&adapter->tx_active) != 0 || any_slot_is_active(adapter)) {
      return WL_ERR_WOULD_BLOCK;
    }
    atomic_set(&adapter->started, 0);
    atomic_set(&adapter->stopping, 0);
    atomic_set(&adapter->paused, 0);
    atomic_set(&adapter->expected_disabled, 0);
    return WL_OK;
  }
  if (atomic_get(&adapter->started) != 0 &&
      atomic_get(&adapter->running) == 0 && atomic_get(&adapter->paused) != 0) {
    if (any_slot_is_active(adapter)) {
      return WL_ERR_WOULD_BLOCK;
    }
    if (adapter->gap_pending != 0U) {
      adapter->gap_pending = 0U;
      wl_rx_note_overflow(adapter->config.link);
      atomic_set(&adapter->recovery_barrier, 1);
      return WL_ERR_WOULD_BLOCK;
    }
    return start_rx(adapter);
  }
  return WL_OK;
}

void wl_zephyr_uart_dma_reset_stats(wl_zephyr_uart_dma_t *adapter) {
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

void wl_zephyr_uart_dma_get_stats(const wl_zephyr_uart_dma_t *adapter,
                                  wl_zephyr_uart_dma_stats_t *out_stats) {
  if (adapter == NULL || out_stats == NULL) {
    return;
  }
  *out_stats = (wl_zephyr_uart_dma_stats_t){
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
