/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/zephyr/uart_dma.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static wl_zephyr_uart_dma_slot_t *find_slot(wl_zephyr_uart_dma_t *adapter,
                                            uint8_t *data) {
  for (size_t i = 0U; i < WL_RX_DMA_MAX_CLAIMS; ++i) {
    wl_zephyr_uart_dma_slot_t *slot = &adapter->slots[i];

    if (slot->claim.token != 0U && slot->claim.span.data == data) {
      return slot;
    }
  }
  return NULL;
}

static wl_zephyr_uart_dma_slot_t *find_free_slot(
    wl_zephyr_uart_dma_t *adapter) {
  for (size_t i = 0U; i < WL_RX_DMA_MAX_CLAIMS; ++i) {
    if (adapter->slots[i].claim.token == 0U) {
      return &adapter->slots[i];
    }
  }
  return NULL;
}

static bool driver_owns_any_slot(const wl_zephyr_uart_dma_t *adapter) {
  for (size_t i = 0U; i < WL_RX_DMA_MAX_CLAIMS; ++i) {
    if (adapter->slots[i].claim.token != 0U &&
        !atomic_test_bit(&adapter->released_slots, (int)i)) {
      return true;
    }
  }
  return false;
}

static bool any_slot_is_active(const wl_zephyr_uart_dma_t *adapter) {
  for (size_t i = 0U; i < WL_RX_DMA_MAX_CLAIMS; ++i) {
    if (adapter->slots[i].claim.token != 0U) {
      return true;
    }
  }
  return false;
}

static int publish_received_prefix(wl_zephyr_uart_dma_t *adapter,
                                   wl_zephyr_uart_dma_slot_t *slot,
                                   bool measure_cycles) {
  size_t received = slot->received;
  size_t published = slot->published;
  size_t new_length;
  uint32_t started = 0U;
  int ret;

  if (received <= published) {
    return WL_OK;
  }
  new_length = received - published;
  if (measure_cycles && adapter->config.cycle_counter != NULL) {
    started =
        adapter->config.cycle_counter(adapter->config.cycle_counter_user_data);
  }
  if (adapter->config.complete_from_dma != NULL) {
    adapter->config.complete_from_dma(adapter->config.cache_user_data,
                                      slot->claim.span.data + published,
                                      new_length);
  }
  ret = wl_rx_dma_publish(adapter->config.link, &slot->claim, published,
                          new_length);
  if (ret != WL_OK) {
    return ret == WL_ERR_INVALID_STATE ? WL_ERR_WOULD_BLOCK : ret;
  }
  slot->published = received;
  atomic_add(&adapter->published_bytes, (atomic_val_t)new_length);
  if (measure_cycles && adapter->config.cycle_counter != NULL) {
    atomic_add(&adapter->producer_cycles,
               (atomic_val_t)(adapter->config.cycle_counter(
                                  adapter->config.cycle_counter_user_data) -
                              started));
  }
  return WL_OK;
}

/* Some UART drivers invoke callbacks in an ISR; finish in consumer context. */
static int finish_released_slots(wl_zephyr_uart_dma_t *adapter) {
  bool progressed;

  do {
    progressed = false;
    for (size_t i = 0U; i < WL_RX_DMA_MAX_CLAIMS; ++i) {
      wl_zephyr_uart_dma_slot_t *slot = &adapter->slots[i];

      int ret;

      /* released_slots is the ISR-to-consumer ownership handoff. Do not read
       * the non-atomic slot progress fields while the driver still owns it. */
      if (!atomic_test_bit(&adapter->released_slots, (int)i)) {
        continue;
      }
      ret = publish_received_prefix(adapter, slot, false);
      if (ret != WL_OK && ret != WL_ERR_WOULD_BLOCK) {
        return ret;
      }
      if (ret == WL_ERR_WOULD_BLOCK) {
        continue;
      }
      ret = wl_rx_dma_finish(adapter->config.link, &slot->claim);
      if (ret == WL_OK) {
        atomic_clear_bit(&adapter->released_slots, (int)i);
        memset(slot, 0, sizeof(*slot));
        progressed = true;
        break;
      }
      if (ret != WL_ERR_INVALID_STATE && ret != WL_ERR_WOULD_BLOCK) {
        return ret;
      }
    }
  } while (progressed);

  return WL_OK;
}

static void mark_abort(wl_zephyr_uart_dma_t *adapter) {
  atomic_set(&adapter->abort_pending, 1);
  atomic_inc(&adapter->errors);
}

static int supply_buffer(wl_zephyr_uart_dma_t *adapter, bool first) {
  wl_zephyr_uart_dma_slot_t *slot = find_free_slot(adapter);
  size_t slot_index;
  int ret;

  if (slot == NULL) {
    return WL_ERR_WOULD_BLOCK;
  }
  slot_index = (size_t)(slot - adapter->slots);
  ret = wl_rx_dma_claim(adapter->config.link, adapter->config.maximum_chunk,
                        &slot->claim);
  if (ret != WL_OK) {
    return ret;
  }
  slot->received = 0U;
  slot->published = 0U;
  if (adapter->config.prepare_for_dma != NULL) {
    adapter->config.prepare_for_dma(adapter->config.cache_user_data,
                                    slot->claim.span.data,
                                    slot->claim.span.length);
  }
  ret = first
            ? uart_rx_enable(adapter->config.uart, slot->claim.span.data,
                             slot->claim.span.length, adapter->config.timeout_us)
            : uart_rx_buf_rsp(adapter->config.uart, slot->claim.span.data,
                              slot->claim.span.length);
  if (ret != 0) {
    atomic_set_bit(&adapter->released_slots, (int)slot_index);
    mark_abort(adapter);
    return ret;
  }
  return WL_OK;
}

static void uart_dma_callback(const struct device *dev, struct uart_event *event,
                              void *user_data) {
  wl_zephyr_uart_dma_t *adapter = user_data;

  if (adapter == NULL || dev != adapter->config.uart) {
    return;
  }
  switch (event->type) {
  case UART_RX_RDY: {
    wl_zephyr_uart_dma_slot_t *slot = find_slot(adapter, event->data.rx.buf);
    size_t end;
    int ret;

    atomic_inc(&adapter->rx_ready_events);
    if (slot == NULL || event->data.rx.offset > slot->claim.span.length ||
        event->data.rx.len > slot->claim.span.length - event->data.rx.offset) {
      mark_abort(adapter);
      break;
    }
    end = event->data.rx.offset + event->data.rx.len;
    if (end > slot->received) {
      slot->received = end;
      ret = publish_received_prefix(adapter, slot, true);
      if (ret != WL_OK && ret != WL_ERR_WOULD_BLOCK) {
        mark_abort(adapter);
      }
    }
    break;
  }
  case UART_RX_BUF_REQUEST:
    atomic_inc(&adapter->buffer_requests);
    /* Finite-timeout drivers may release a short buffer. Keep that mode to
     * one MTU-sized claim so the unwritten tail can be reclaimed safely. */
    if (adapter->config.timeout_us != SYS_FOREVER_US) {
      atomic_set(&adapter->expected_disabled, 1);
      atomic_set(&adapter->paused, 1);
      break;
    }
    if (supply_buffer(adapter, false) != WL_OK) {
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
    atomic_set_bit(&adapter->released_slots, (int)(slot - adapter->slots));
    break;
  }
  case UART_RX_STOPPED:
    mark_abort(adapter);
    break;
  case UART_RX_DISABLED:
    atomic_set(&adapter->running, 0);
    if (atomic_get(&adapter->paused) != 0 &&
        atomic_get(&adapter->abort_pending) == 0 &&
        !atomic_cas(&adapter->expected_disabled, 1, 0)) {
      mark_abort(adapter);
    }
    break;
  default:
    break;
  }
}

int wl_zephyr_uart_dma_init(wl_zephyr_uart_dma_t *adapter,
                            const wl_zephyr_uart_dma_config_t *config) {
  if (adapter == NULL || config == NULL || config->uart == NULL ||
      config->link == NULL || config->maximum_chunk == 0U ||
      (config->timeout_us < 0 && config->timeout_us != SYS_FOREVER_US) ||
      !device_is_ready(config->uart)) {
    return WL_ERR_INVALID_ARG;
  }
  memset(adapter, 0, sizeof(*adapter));
  adapter->config = *config;
  return uart_callback_set(config->uart, uart_dma_callback, adapter) == 0
             ? WL_OK
             : WL_ERR_NOT_SUPPORTED;
}

int wl_zephyr_uart_dma_start(wl_zephyr_uart_dma_t *adapter) {
  int ret;

  if (adapter == NULL || adapter->config.link == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (atomic_get(&adapter->running) != 0 || any_slot_is_active(adapter)) {
    return WL_ERR_BUSY;
  }
  /* uart_rx_enable() may synchronously request its look-ahead buffer. */
  atomic_set(&adapter->paused, 0);
  atomic_set(&adapter->expected_disabled, 0);
  atomic_set(&adapter->running, 1);
  ret = supply_buffer(adapter, true);
  if (ret != WL_OK) {
    atomic_set(&adapter->running, 0);
    atomic_set(&adapter->paused, 1);
    return ret;
  }
  return WL_OK;
}

int wl_zephyr_uart_dma_service(wl_zephyr_uart_dma_t *adapter) {
  int ret;

  if (adapter == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (atomic_get(&adapter->abort_pending) != 0) {
    if (driver_owns_any_slot(adapter)) {
      return WL_ERR_WOULD_BLOCK;
    }
    if (wl_rx_dma_abort(adapter->config.link) != WL_OK) {
      return WL_ERR_INVALID_STATE;
    }
    memset(adapter->slots, 0, sizeof(adapter->slots));
    atomic_set(&adapter->released_slots, 0);
    atomic_set(&adapter->abort_pending, 0);
    atomic_set(&adapter->expected_disabled, 0);
    atomic_set(&adapter->paused, 1);
    atomic_set(&adapter->recovery_barrier, 1);
    return WL_ERR_WOULD_BLOCK;
  }
  ret = finish_released_slots(adapter);
  if (ret != WL_OK) {
    mark_abort(adapter);
    return WL_ERR_INVALID_STATE;
  }
  if (atomic_get(&adapter->recovery_barrier) != 0) {
    atomic_set(&adapter->recovery_barrier, 0);
    return WL_ERR_WOULD_BLOCK;
  }
  if (atomic_get(&adapter->running) == 0 &&
      atomic_get(&adapter->paused) != 0) {
    /* RX_DISABLED is the release handoff barrier. Reap again after observing
     * it so a callback racing the first pass cannot leave a short predecessor
     * active while start() allocates a successor. */
    ret = finish_released_slots(adapter);
    if (ret != WL_OK) {
      mark_abort(adapter);
      return WL_ERR_INVALID_STATE;
    }
    if (any_slot_is_active(adapter)) {
      return WL_ERR_WOULD_BLOCK;
    }
    return wl_zephyr_uart_dma_start(adapter);
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
      .errors = (uint32_t)atomic_get(&adapter->errors),
      .running = atomic_get(&adapter->running) != 0 ? 1U : 0U,
      .paused = atomic_get(&adapter->paused) != 0 ? 1U : 0U,
  };
}
