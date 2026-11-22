/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "wirelink/zephyr/uart_irq.h"

int main(void) {
  const struct device *cdc = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
  if (!device_is_ready(cdc)) { printk("CDC device unavailable\n"); return 1; }
  const wl_err_t error = wl_zephyr_uart_irq_probe(cdc);
  if (error == WL_ERR_NOT_SUPPORTED) {
    printk("CDC IRQ unsupported: driver lacks the required physical completion API\n");
    return 0;
  }
  if (error == WL_OK) {
    printk("CDC IRQ API present: physical drain semantics still require qualification\n");
    return 0;
  }
  printk("CDC IRQ capability probe failed: %d\n", error); return 1;
}
