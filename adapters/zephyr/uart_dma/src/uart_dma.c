/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/zephyr/uart_dma.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>

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
    if (adapter->slots[i].driver_owned != 0U) {
      return true;
    }
  }
  return false;
}

static void mark_abort(wl_zephyr_uart_dma_t *adapter) {
  atomic_set(&adapter->abort_pending, 1);
  atomic_inc(&adapter->errors);
}

static int supply_buffer(wl_zephyr_uart_dma_t *adapter, bool first) {
  wl_zephyr_uart_dma_slot_t *slot = find_free_slot(adapter);
  int ret;

  if (slot == NULL) {
    return WL_ERR_WOULD_BLOCK;
  }
  ret = wl_rx_dma_claim(adapter->config.link, adapter->config.maximum_chunk,
                        &slot->claim);
  if (ret != WL_OK) {
    return ret;
  }
  slot->published = 0U;
  slot->driver_owned = 1U;
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
    slot->driver_owned = 0U;
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
    wl_zephyr_uart_dma_slot_t *slot =
        find_slot(adapter, event->data.rx.buf);
    size_t end;

    atomic_inc(&adapter->rx_ready_events);
    if (slot == NULL || event->data.rx.offset > slot->published ||
        event->data.rx.len > slot->claim.span.length - event->data.rx.offset) {
      mark_abort(adapter);
      break;
    }
    end = event->data.rx.offset + event->data.rx.len;
    if (end > slot->published) {
      size_t new_length = end - slot->published;

      if (adapter->config.complete_from_dma != NULL) {
        adapter->config.complete_from_dma(adapter->config.cache_user_data,
                                          slot->claim.span.data + slot->published,
                                          new_length);
      }
      if (wl_rx_dma_publish(adapter->config.link, &slot->claim,
                            slot->published, new_length) != WL_OK) {
        mark_abort(adapter);
        break;
      }
      slot->published = end;
      atomic_add(&adapter->published_bytes, (atomic_val_t)new_length);
    }
    break;
  }
  case UART_RX_BUF_REQUEST:
    atomic_inc(&adapter->buffer_requests);
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
    slot->driver_owned = 0U;
    if (slot->published != slot->claim.span.length ||
        wl_rx_dma_finish(adapter->config.link, &slot->claim) != WL_OK) {
      mark_abort(adapter);
      break;
    }
    memset(slot, 0, sizeof(*slot));
    break;
  }
  case UART_RX_STOPPED:
    mark_abort(adapter);
    break;
  case UART_RX_DISABLED:
    atomic_set(&adapter->running, 0);
    if (atomic_get(&adapter->paused) != 0 &&
        atomic_get(&adapter->abort_pending) == 0) {
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
      config->timeout_us < 0 || !device_is_ready(config->uart)) {
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
  if (atomic_get(&adapter->running) != 0 ||
      driver_owns_any_slot(adapter)) {
    return WL_ERR_BUSY;
  }
  ret = supply_buffer(adapter, true);
  if (ret != WL_OK) {
    atomic_set(&adapter->paused, 1);
    return ret;
  }
  atomic_set(&adapter->paused, 0);
  atomic_set(&adapter->running, 1);
  return WL_OK;
}

int wl_zephyr_uart_dma_service(wl_zephyr_uart_dma_t *adapter) {
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
    atomic_set(&adapter->abort_pending, 0);
    atomic_set(&adapter->paused, 1);
    atomic_set(&adapter->recovery_barrier, 1);
    return WL_ERR_WOULD_BLOCK;
  }
  if (atomic_get(&adapter->recovery_barrier) != 0) {
    atomic_set(&adapter->recovery_barrier, 0);
    return WL_ERR_WOULD_BLOCK;
  }
  if (atomic_get(&adapter->running) == 0 &&
      atomic_get(&adapter->paused) != 0) {
    return wl_zephyr_uart_dma_start(adapter);
  }
  return WL_OK;
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
      .errors = (uint32_t)atomic_get(&adapter->errors),
      .running = atomic_get(&adapter->running) != 0 ? 1U : 0U,
      .paused = atomic_get(&adapter->paused) != 0 ? 1U : 0U,
  };
}
