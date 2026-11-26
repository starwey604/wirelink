/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_DETAIL_UART_H_
#define WIRELINK_ZEPHYR_DETAIL_UART_H_

#include "wirelink/types.h"
#include <errno.h>
#include <zephyr/drivers/uart.h>

static inline wl_err_t wl_zephyr_uart_set_baud(const struct device *uart, uint32_t baud_rate) {
  if (!baud_rate) return WL_OK;
  const struct uart_config line = {
    .baudrate = baud_rate, .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1,
    .data_bits = UART_CFG_DATA_BITS_8, .flow_ctrl = UART_CFG_FLOW_CTRL_NONE
  };
  const int ret = uart_configure(uart, &line);
  if (ret == -ENOSYS || ret == -ENOTSUP) return WL_ERR_NOT_SUPPORTED;
  return ret == 0 ? WL_OK : WL_ERR_IO;
}

#endif
