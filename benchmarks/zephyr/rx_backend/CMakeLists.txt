# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_rx_bipbuf_bench)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(WIRELINK_BENCH_INGRESS "IRQ" CACHE STRING
    "Wirelink RX ingress: IRQ, DMA, or USB")
set(WIRELINK_BENCH_STREAM OFF CACHE BOOL
    "Measure a continuous back-to-back frame stream instead of stop-and-wait")
set(WIRELINK_BENCH_RING "" CACHE STRING
    "Override the usable RX ring size (BENCH_RX_USABLE) in bytes")
set(WIRELINK_BENCH_DMA_CHUNK "" CACHE STRING
    "Override each UART DMA staging buffer size (BENCH_DMA_MAX_CHUNK) in bytes")
set(WIRELINK_BENCH_STREAM_FRAMES "" CACHE STRING
    "Measured frames per stream profile (BENCH_STREAM_FRAMES)")
set(WIRELINK_BENCH_PAYLOAD_START "" CACHE STRING
    "First payload profile index (BENCH_PAYLOAD_START_INDEX)")
set(WIRELINK_BENCH_PAYLOAD_END "" CACHE STRING
    "One past the last payload profile index (BENCH_PAYLOAD_END_INDEX)")

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
  ${WIRELINK_ROOT}/src/unit_rx.c
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

if(WIRELINK_BENCH_STREAM)
  target_compile_definitions(app PRIVATE WL_BENCH_STREAM=1)
endif()
if(NOT WIRELINK_BENCH_RING STREQUAL "")
  target_compile_definitions(app PRIVATE BENCH_RX_USABLE=${WIRELINK_BENCH_RING})
endif()
if(NOT WIRELINK_BENCH_DMA_CHUNK STREQUAL "")
  target_compile_definitions(app PRIVATE
    BENCH_DMA_MAX_CHUNK=${WIRELINK_BENCH_DMA_CHUNK})
endif()
if(NOT WIRELINK_BENCH_STREAM_FRAMES STREQUAL "")
  target_compile_definitions(app PRIVATE
    BENCH_STREAM_FRAMES=${WIRELINK_BENCH_STREAM_FRAMES})
endif()
if(NOT WIRELINK_BENCH_PAYLOAD_START STREQUAL "")
  target_compile_definitions(app PRIVATE
    BENCH_PAYLOAD_START_INDEX=${WIRELINK_BENCH_PAYLOAD_START})
endif()
if(NOT WIRELINK_BENCH_PAYLOAD_END STREQUAL "")
  target_compile_definitions(app PRIVATE
    BENCH_PAYLOAD_END_INDEX=${WIRELINK_BENCH_PAYLOAD_END})
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
