# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_frame_unit)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

target_sources(testbinary PRIVATE
  src/main.c
  ${WIRELINK_ROOT}/src/frame.c
  ${WIRELINK_ROOT}/src/crc.c
  ${WIRELINK_ROOT}/src/cobs.c
)

target_include_directories(testbinary PRIVATE
  ${WIRELINK_ROOT}/include
)
