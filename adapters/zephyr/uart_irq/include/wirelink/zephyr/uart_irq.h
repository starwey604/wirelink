/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_ZEPHYR_UART_IRQ_H_
#define WIRELINK_ZEPHYR_UART_IRQ_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>

#include "wirelink/wirelink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wl_zephyr_uart_irq_config {
  const struct device *uart;
  wl_ctx_t *link;
} wl_zephyr_uart_irq_config_t;

typedef struct wl_zephyr_uart_irq_stats {
  uint32_t rx_irqs;
  uint32_t rx_bytes;
  uint32_t rx_pauses;
  uint32_t tx_irqs;
  uint32_t tx_bytes;
  uint32_t tx_completions;
  uint32_t errors;
  bool started;
  bool rx_paused;
  bool tx_active;
} wl_zephyr_uart_irq_stats_t;

typedef struct wl_zephyr_uart_irq {
  const struct device *uart;
  wl_ctx_t *link;
  const uint8_t *tx_data;
  size_t tx_length;
  size_t tx_offset;
  wl_io_token_t tx_token;
  atomic_t flags;
  atomic_t tx_completion;
  atomic_t rx_irqs;
  atomic_t rx_bytes;
  atomic_t rx_pauses;
  atomic_t tx_irqs;
  atomic_t tx_bytes;
  atomic_t tx_completions;
  atomic_t errors;
} wl_zephyr_uart_irq_t;

int wl_zephyr_uart_irq_init(wl_zephyr_uart_irq_t *adapter,
                            const wl_zephyr_uart_irq_config_t *config);
int wl_zephyr_uart_irq_start(wl_zephyr_uart_irq_t *adapter);
int wl_zephyr_uart_irq_service(wl_zephyr_uart_irq_t *adapter);
void wl_zephyr_uart_irq_get_stats(const wl_zephyr_uart_irq_t *adapter,
                                  wl_zephyr_uart_irq_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* WIRELINK_ZEPHYR_UART_IRQ_H_ */
