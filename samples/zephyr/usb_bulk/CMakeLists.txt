# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_usb_bulk)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
set(WIRELINK_WLC_GENERATED_DIR
  "${WIRELINK_ROOT}/tests/fixtures/wlc/generated/current")

target_sources(app PRIVATE
  src/main.c
  ${WIRELINK_WLC_GENERATED_DIR}/control.c
)
target_include_directories(app PRIVATE
  ${WIRELINK_WLC_GENERATED_DIR}
  ${WIRELINK_ROOT}/src
)
