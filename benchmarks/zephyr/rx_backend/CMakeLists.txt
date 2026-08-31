# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_rx_bipbuf_bench)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(WIRELINK_BENCH_INGRESS "IRQ" CACHE STRING
    "Wirelink RX ingress: IRQ, DMA, or USB")

set(WIRELINK_CORE_SOURCES
  ${WIRELINK_ROOT}/src/cobs.c
  ${WIRELINK_ROOT}/src/crc.c
  ${WIRELINK_ROOT}/src/frame.c
  ${WIRELINK_ROOT}/src/init.c
  ${WIRELINK_ROOT}/src/protocol.c
  ${WIRELINK_ROOT}/src/rx_ring_bipbuf.c
  ${WIRELINK_ROOT}/src/schedule.c
  ${WIRELINK_ROOT}/src/transport.c
  ${WIRELINK_ROOT}/src/types.c
)

if(WIRELINK_BENCH_INGRESS STREQUAL "IRQ")
  target_compile_definitions(app PRIVATE WL_BENCH_INGRESS_IRQ=1)
elseif(WIRELINK_BENCH_INGRESS STREQUAL "DMA")
  target_compile_definitions(app PRIVATE WL_BENCH_INGRESS_DMA=1)
elseif(WIRELINK_BENCH_INGRESS STREQUAL "USB")
  target_compile_definitions(app PRIVATE WL_BENCH_INGRESS_USB=1)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
else()
  message(FATAL_ERROR "WIRELINK_BENCH_INGRESS must be IRQ, DMA, or USB")
endif()

target_sources(app PRIVATE
  src/main.c
  ${WIRELINK_CORE_SOURCES}
  ${WIRELINK_ROOT}/adapters/zephyr/uart_dma/src/uart_dma.c
)
target_include_directories(app PRIVATE
  ${WIRELINK_ROOT}/include
  ${WIRELINK_ROOT}/adapters/zephyr/uart_dma/include
)
