# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_framing_workload_unit)
set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
target_sources(testbinary PRIVATE
  src/main.c
  ${WIRELINK_ROOT}/benchmarks/framing/workload.c
  ${WIRELINK_ROOT}/src/cobs.c
  ${WIRELINK_ROOT}/src/crc.c
  ${WIRELINK_ROOT}/src/frame.c
  ${WIRELINK_ROOT}/src/frame_cobs_crc16.c
  ${WIRELINK_ROOT}/src/init.c
  ${WIRELINK_ROOT}/src/protocol.c
  ${WIRELINK_ROOT}/src/unit_frame.c
  ${WIRELINK_ROOT}/src/rx_ring_bipbuf.c
  ${WIRELINK_ROOT}/src/schedule.c
  ${WIRELINK_ROOT}/src/transport.c
  ${WIRELINK_ROOT}/src/unit_rx.c
  ${WIRELINK_ROOT}/src/types.c)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/include ${WIRELINK_ROOT}/src
  ${WIRELINK_ROOT}/benchmarks/framing)
set_property(SOURCE ${WIRELINK_ROOT}/src/frame.c ${WIRELINK_ROOT}/src/frame_cobs_crc16.c APPEND PROPERTY COMPILE_OPTIONS -finstrument-functions)
