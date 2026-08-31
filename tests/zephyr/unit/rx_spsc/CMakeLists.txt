# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_rx_spsc_unit)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

target_sources(testbinary PRIVATE
  ${WIRELINK_ROOT}/tests/zephyr/common/rx_spsc/src/main.c
  ${WIRELINK_ROOT}/src/cobs.c
  ${WIRELINK_ROOT}/src/crc.c
  ${WIRELINK_ROOT}/src/frame.c
  ${WIRELINK_ROOT}/src/init.c
  ${WIRELINK_ROOT}/src/protocol.c
  ${WIRELINK_ROOT}/src/rx_ring_bipbuf.c
  ${WIRELINK_ROOT}/src/transport.c
  ${WIRELINK_ROOT}/src/types.c
  ${WIRELINK_ROOT}/src/unit_rx.c
)

target_include_directories(testbinary PRIVATE
  ${WIRELINK_ROOT}/include
  ${WIRELINK_ROOT}/src
)
