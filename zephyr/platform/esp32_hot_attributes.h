/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_ESP32_HOT_ATTRIBUTES_H
#define WIRELINK_ZEPHYR_ESP32_HOT_ATTRIBUTES_H

/* Single fixed .iram1 section name avoids __COUNTER__ mismatch between
 * header declarations and .c definitions of WL_HOT_ATTR functions. */
#define WL_HOT_ATTR __attribute__((section(".iram1.wl_hot")))

#endif
