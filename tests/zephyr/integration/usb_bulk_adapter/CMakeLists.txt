# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_usb_bulk_adapter)

if(TEST_USB_NATIVE)
  target_sources(app PRIVATE src/native.c)
  target_compile_definitions(app PRIVATE TEST_USB_NATIVE_ERROR=$<BOOL:${TEST_USB_NATIVE_ERROR}>)
else()
  target_sources(app PRIVATE src/main.c)
endif()
target_sources(app PRIVATE
  ${WIRELINK_ROOT}/adapters/zephyr/usb_bulk/src/usb_bulk.c
)

target_include_directories(app PRIVATE ${WIRELINK_ROOT}/src)

zephyr_ld_options(
  -Wl,--wrap=usbd_ep_enqueue
  -Wl,--wrap=usbd_disable
  -Wl,--wrap=usbd_enable
  -Wl,--wrap=usbd_ep_buf_free
)
