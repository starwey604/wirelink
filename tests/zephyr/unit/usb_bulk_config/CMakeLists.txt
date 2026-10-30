# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_usb_bulk_config)
set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
target_sources(testbinary PRIVATE src/main.c)
foreach(source cobs crc frame frame_cobs_crc16 init protocol unit_frame rx_ring_bipbuf schedule transport types unit_rx)
  target_sources(testbinary PRIVATE ${WIRELINK_ROOT}/src/${source}.c)
endforeach()
target_include_directories(testbinary PRIVATE
  ${WIRELINK_ROOT}/include ${WIRELINK_ROOT}/src
  ${WIRELINK_ROOT}/adapters/common/include)
