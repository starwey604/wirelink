/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "wirelink/zephyr/kernel_uart_irq.h"

static wl_kernel_uart_irq_t adapter;
static wl_packet_port_t port;
static wl_packet_stream_t stream;
static wl_byte_fifo_t fifo;
static uint8_t rx[2][65], tx[80], staging[128];

int main(void) {
  const struct device *cdc = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
  if (!device_is_ready(cdc)) { printk("CDC device unavailable\n"); return 1; }
  /* Diagnostic only: no USB enumeration, physical RX or TX is started. This
   * fixed local identity never represents a communication instance on wire. */
  wl_packet_config_t packet = {.instance = 1, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx),
    .submit = wl_packet_stream_cobs_crc16_submit, .notify = wl_packet_stream_notify,
    .user = &stream};
  if (wl_packet_init(&port, &packet) != WL_OK) return 1;
  if (wl_byte_fifo_init(&fifo, (wl_span_t){staging, sizeof(staging)}, NULL, NULL) != WL_OK) return 1;
  wl_packet_stream_config_t connector = {.port = &port, .tx_storage = {tx, sizeof(tx)},
    .maximum_frame_size = 64, .physical_submit = wl_kernel_uart_irq_submit,
    .notify = wl_kernel_uart_irq_notify, .user = &adapter};
  if (wl_packet_stream_cobs_crc16_init(&stream, &connector) != WL_OK) return 1;
  wl_kernel_uart_irq_config_t physical = {.uart = cdc, .port = &port,
    .fifo = &fifo, .stream = &stream};
  const int error = wl_kernel_uart_irq_init(&adapter, &physical);
  if (error == WL_ERR_NOT_SUPPORTED) {
    wl_packet_stop(&port);
    printk("CDC IRQ unsupported: driver lacks the required physical completion API\n");
    return 0;
  }
  if (error == WL_OK) {
    wl_kernel_uart_irq_stop(&adapter);
    printk("CDC IRQ API present: physical drain semantics still require qualification\n");
    return 0;
  }
  wl_packet_stop(&port);
  printk("CDC IRQ capability probe failed: %d\n", error); return 1;
}
