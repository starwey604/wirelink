/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_UART_IRQ_ESP32_ATTRIBUTES_H
#define WIRELINK_ZEPHYR_UART_IRQ_ESP32_ATTRIBUTES_H

/* Supplied by the platform build, never included by the portable adapter.
 * Match the ESP linker input convention without importing ESP-IDF headers.
 * Expand the counter at each function, as in the previous platform mapping. */
#define WL_UART_IRQ_SECTION_NUMBER_(number) #number
#define WL_UART_IRQ_SECTION_NUMBER(number) WL_UART_IRQ_SECTION_NUMBER_(number)
#define WL_ZEPHYR_UART_IRQ_HOT_CODE \
  __attribute__((section(".iram1." WL_UART_IRQ_SECTION_NUMBER(__COUNTER__))))

#endif
