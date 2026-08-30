/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/zephyr/uart_irq.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <zephyr/drivers/uart.h>

enum adapter_flag {
  ADAPTER_INITIALIZED,
  ADAPTER_STARTED,
  ADAPTER_RX_PAUSED,
  ADAPTER_TX_ACTIVE,
};

enum tx_completion {
  TX_COMPLETION_NONE,
  TX_COMPLETION_DONE,
  TX_COMPLETION_FAILED,
};

static wl_sink_result_t uart_sink(void *user_data, wl_io_token_t token,
                                  const uint8_t *data, size_t length) {
  wl_zephyr_uart_irq_t *adapter = user_data;

  if (adapter == NULL || data == NULL || length == 0U ||
      !atomic_test_bit(&adapter->flags, ADAPTER_STARTED)) {
    return WL_SINK_FAILED;
  }
  if (atomic_test_and_set_bit(&adapter->flags, ADAPTER_TX_ACTIVE)) {
    return WL_SINK_BUSY;
  }

  adapter->tx_data = data;
  adapter->tx_length = length;
  adapter->tx_offset = 0U;
  adapter->tx_token = token;
  atomic_set(&adapter->tx_completion, TX_COMPLETION_NONE);
  uart_irq_tx_enable(adapter->uart);
  return WL_SINK_STARTED;
}

static bool receive_ready(wl_zephyr_uart_irq_t *adapter) {
  wl_span_t span;
  size_t request;
  int received;
  int result;

  result = wl_rx_reserve(adapter->link, &span);
  if (result != WL_OK || span.length == 0U) {
    if (result == WL_OK) {
      (void)wl_rx_commit(adapter->link, 0U);
    }
    uart_irq_rx_disable(adapter->uart);
    atomic_set_bit(&adapter->flags, ADAPTER_RX_PAUSED);
    atomic_inc(&adapter->rx_pauses);
    return true;
  }

  request = span.length > (size_t)INT_MAX ? (size_t)INT_MAX : span.length;
  received = uart_fifo_read(adapter->uart, span.data, (int)request);
  if (received <= 0) {
    (void)wl_rx_commit(adapter->link, 0U);
    if (received < 0) {
      atomic_inc(&adapter->errors);
    }
    return false;
  }
  if (wl_rx_commit(adapter->link, (size_t)received) != WL_OK) {
    atomic_inc(&adapter->errors);
    return true;
  }
  atomic_add(&adapter->rx_bytes, received);
  return true;
}

static bool transmit_ready(wl_zephyr_uart_irq_t *adapter) {
  size_t remaining;
  size_t request;
  int sent;

  if (!atomic_test_bit(&adapter->flags, ADAPTER_TX_ACTIVE)) {
    uart_irq_tx_disable(adapter->uart);
    return true;
  }

  remaining = adapter->tx_length - adapter->tx_offset;
  if (remaining > 0U) {
    request = remaining > (size_t)INT_MAX ? (size_t)INT_MAX : remaining;
    sent = uart_fifo_fill(adapter->uart, adapter->tx_data + adapter->tx_offset,
                          (int)request);
    if (sent < 0) {
      uart_irq_tx_disable(adapter->uart);
      atomic_set(&adapter->tx_completion, TX_COMPLETION_FAILED);
      atomic_inc(&adapter->errors);
      return true;
    }
    adapter->tx_offset += (size_t)sent;
    atomic_add(&adapter->tx_bytes, sent);
    if (sent == 0) {
      return false;
    }
  }

  if (adapter->tx_offset == adapter->tx_length &&
      adapter->tx_length != 0U) {
    const int complete = uart_irq_tx_complete(adapter->uart);

    /* CDC ACM has copied the bytes into its private FIFO but exposes no
     * physical-idle query. That is already the borrowed-buffer ownership
     * boundary; hardware UARTs can wait for their real idle indication. */
    if (complete > 0 || complete == -ENOSYS) {
      uart_irq_tx_disable(adapter->uart);
      atomic_set(&adapter->tx_completion, TX_COMPLETION_DONE);
    } else if (complete < 0) {
      uart_irq_tx_disable(adapter->uart);
      atomic_set(&adapter->tx_completion, TX_COMPLETION_FAILED);
      atomic_inc(&adapter->errors);
    }
  }
  return true;
}

static void uart_callback(const struct device *device, void *user_data) {
  wl_zephyr_uart_irq_t *adapter = user_data;

  while (true) {
    bool progressed = false;

    uart_irq_update(device);
    if (uart_irq_is_pending(device) <= 0) {
      break;
    }
    if (!atomic_test_bit(&adapter->flags, ADAPTER_RX_PAUSED) &&
        uart_irq_rx_ready(device)) {
      atomic_inc(&adapter->rx_irqs);
      progressed = receive_ready(adapter) || progressed;
    }
    if (uart_irq_tx_ready(device) > 0) {
      atomic_inc(&adapter->tx_irqs);
      progressed = transmit_ready(adapter) || progressed;
    }
    if (!progressed) {
      break;
    }
  }
}

int wl_zephyr_uart_irq_init(wl_zephyr_uart_irq_t *adapter,
                            const wl_zephyr_uart_irq_config_t *config) {
  wl_config_t link_config;
  int result;

  if (adapter == NULL || config == NULL || config->uart == NULL ||
      config->link == NULL || !device_is_ready(config->uart)) {
    return WL_ERR_INVALID_ARG;
  }
  result = wl_get_config(config->link, &link_config);
  if (result != WL_OK || link_config.envelope != WL_ENVELOPE_COBS_STREAM) {
    return result == WL_OK ? WL_ERR_NOT_SUPPORTED : result;
  }

  memset(adapter, 0, sizeof(*adapter));
  adapter->uart = config->uart;
  adapter->link = config->link;
  result = uart_irq_callback_user_data_set(adapter->uart, uart_callback, adapter);
  if (result != 0) {
    return WL_ERR_NOT_SUPPORTED;
  }
  result = wl_set_sink(adapter->link, uart_sink, adapter);
  if (result != WL_OK) {
    return result;
  }
  atomic_set_bit(&adapter->flags, ADAPTER_INITIALIZED);
  return WL_OK;
}

int wl_zephyr_uart_irq_start(wl_zephyr_uart_irq_t *adapter) {
  if (adapter == NULL ||
      !atomic_test_bit(&adapter->flags, ADAPTER_INITIALIZED)) {
    return WL_ERR_INVALID_ARG;
  }
  if (!atomic_test_and_set_bit(&adapter->flags, ADAPTER_STARTED)) {
    uart_irq_rx_enable(adapter->uart);
  }
  return WL_OK;
}

int wl_zephyr_uart_irq_service(wl_zephyr_uart_irq_t *adapter) {
  atomic_val_t completion;
  int result;

  if (adapter == NULL || !atomic_test_bit(&adapter->flags, ADAPTER_STARTED)) {
    return WL_ERR_INVALID_ARG;
  }

  completion = atomic_set(&adapter->tx_completion, TX_COMPLETION_NONE);
  if (completion != TX_COMPLETION_NONE) {
    const wl_io_token_t token = adapter->tx_token;

    adapter->tx_data = NULL;
    adapter->tx_length = 0U;
    adapter->tx_offset = 0U;
    adapter->tx_token = 0U;
    atomic_clear_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
    atomic_inc(&adapter->tx_completions);
    result = wl_tx_complete(adapter->link, token,
                            completion == TX_COMPLETION_DONE ? WL_OK
                                                             : WL_ERR_IO);
    if (result != WL_OK) {
      atomic_inc(&adapter->errors);
      return result;
    }
  }

  if (atomic_test_and_clear_bit(&adapter->flags, ADAPTER_RX_PAUSED)) {
    uart_irq_rx_enable(adapter->uart);
  }
  return WL_OK;
}

void wl_zephyr_uart_irq_get_stats(const wl_zephyr_uart_irq_t *adapter,
                                  wl_zephyr_uart_irq_stats_t *out_stats) {
  if (adapter == NULL || out_stats == NULL) {
    return;
  }
  out_stats->rx_irqs = (uint32_t)atomic_get(&adapter->rx_irqs);
  out_stats->rx_bytes = (uint32_t)atomic_get(&adapter->rx_bytes);
  out_stats->rx_pauses = (uint32_t)atomic_get(&adapter->rx_pauses);
  out_stats->tx_irqs = (uint32_t)atomic_get(&adapter->tx_irqs);
  out_stats->tx_bytes = (uint32_t)atomic_get(&adapter->tx_bytes);
  out_stats->tx_completions = (uint32_t)atomic_get(&adapter->tx_completions);
  out_stats->errors = (uint32_t)atomic_get(&adapter->errors);
  out_stats->started = atomic_test_bit(&adapter->flags, ADAPTER_STARTED);
  out_stats->rx_paused = atomic_test_bit(&adapter->flags, ADAPTER_RX_PAUSED);
  out_stats->tx_active = atomic_test_bit(&adapter->flags, ADAPTER_TX_ACTIVE);
}
