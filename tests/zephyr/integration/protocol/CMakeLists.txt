# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_protocol_integration)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

target_sources(app PRIVATE
  src/main.c
  ${WIRELINK_ROOT}/src/bipbuf.c
  ${WIRELINK_ROOT}/src/cobs.c
  ${WIRELINK_ROOT}/src/crc.c
  ${WIRELINK_ROOT}/src/frame.c
  ${WIRELINK_ROOT}/src/init.c
  ${WIRELINK_ROOT}/src/protocol.c
  ${WIRELINK_ROOT}/src/transport.c
  ${WIRELINK_ROOT}/src/types.c
)

target_include_directories(app PRIVATE ${WIRELINK_ROOT}/include)
