# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_rx_backend_bench)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(WIRELINK_BENCH_BACKEND "BIPBUF" CACHE STRING
    "Wirelink RX backend: BIPBUF or LWRB")
set(WIRELINK_BENCH_INGRESS "IRQ" CACHE STRING
    "Wirelink RX ingress: IRQ, DMA, or USB")

set(WIRELINK_CORE_SOURCES
  ${WIRELINK_ROOT}/src/bipbuf.c
  ${WIRELINK_ROOT}/src/cobs.c
  ${WIRELINK_ROOT}/src/crc.c
  ${WIRELINK_ROOT}/src/frame.c
  ${WIRELINK_ROOT}/src/init.c
  ${WIRELINK_ROOT}/src/protocol.c
  ${WIRELINK_ROOT}/src/transport.c
  ${WIRELINK_ROOT}/src/types.c
)

if(WIRELINK_BENCH_BACKEND STREQUAL "BIPBUF")
  list(APPEND WIRELINK_CORE_SOURCES ${WIRELINK_ROOT}/src/rx_ring_bipbuf.c)
  target_compile_definitions(app PRIVATE WL_BENCH_BACKEND_BIPBUF=1)
elseif(WIRELINK_BENCH_BACKEND STREQUAL "LWRB")
  list(APPEND WIRELINK_CORE_SOURCES
    ${WIRELINK_ROOT}/src/rx_ring_lwrb.c
    ${WIRELINK_ROOT}/third_party/lwrb/src/lwrb.c
  )
  target_include_directories(app PRIVATE
    ${WIRELINK_ROOT}/third_party/lwrb/include
  )
  target_compile_definitions(app PRIVATE WL_BENCH_BACKEND_LWRB=1)
else()
  message(FATAL_ERROR "WIRELINK_BENCH_BACKEND must be BIPBUF or LWRB")
endif()

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

target_sources(app PRIVATE src/main.c ${WIRELINK_CORE_SOURCES})
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/include)
