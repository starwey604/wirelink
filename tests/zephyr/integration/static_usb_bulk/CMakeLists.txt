# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_static_usb_bulk)
target_sources(app PRIVATE src/main.c)
zephyr_ld_options(-Wl,--wrap=usbd_ep_enqueue -Wl,--wrap=usbd_disable
  -Wl,--wrap=usbd_enable -Wl,--wrap=usbd_ep_buf_free)
