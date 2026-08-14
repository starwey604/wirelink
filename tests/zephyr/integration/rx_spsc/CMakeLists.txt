# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_rx_spsc_integration)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(WIRELINK_TEST_RX_BACKEND "BIPBUF" CACHE STRING
    "RX ring backend used by the shared conformance suite")

if(WIRELINK_TEST_RX_BACKEND STREQUAL "BIPBUF")
  set(WIRELINK_RX_RING_SOURCES ${WIRELINK_ROOT}/src/rx_ring_bipbuf.c)
elseif(WIRELINK_TEST_RX_BACKEND STREQUAL "LWRB")
  set(WIRELINK_RX_RING_SOURCES
    ${WIRELINK_ROOT}/src/rx_ring_lwrb.c
    ${WIRELINK_ROOT}/third_party/lwrb/src/lwrb.c
  )
  target_include_directories(app PRIVATE
    ${WIRELINK_ROOT}/third_party/lwrb/include
  )
else()
  message(FATAL_ERROR "Unsupported WIRELINK_TEST_RX_BACKEND")
endif()

target_sources(app PRIVATE
  ${WIRELINK_ROOT}/tests/zephyr/common/rx_spsc/src/main.c
  ${WIRELINK_ROOT}/src/bipbuf.c
  ${WIRELINK_ROOT}/src/cobs.c
  ${WIRELINK_ROOT}/src/crc.c
  ${WIRELINK_ROOT}/src/frame.c
  ${WIRELINK_ROOT}/src/init.c
  ${WIRELINK_ROOT}/src/protocol.c
  ${WIRELINK_RX_RING_SOURCES}
  ${WIRELINK_ROOT}/src/transport.c
  ${WIRELINK_ROOT}/src/types.c
)

target_include_directories(app PRIVATE ${WIRELINK_ROOT}/include)
