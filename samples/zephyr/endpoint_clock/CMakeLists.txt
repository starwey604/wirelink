# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)
set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_endpoint_clock_hil)
include(${WIRELINK_ROOT}/cmake/WirelinkWlc.cmake)
wirelink_wlc_generate_codec(TARGET calculator_codec
  SCHEMA "${WIRELINK_ROOT}/examples/01_rpc/calculator.wl")
wirelink_wlc_generate_runtime(TARGET calculator_protocol CODEC_TARGET calculator_codec
  PROFILE "${WIRELINK_ROOT}/examples/01_rpc/calculator.bind.wl")
target_link_libraries(calculator_codec PUBLIC zephyr_interface)
target_sources(app PRIVATE src/main.c ${WIRELINK_ROOT}/adapters/loopback/src/loopback.c)
target_link_libraries(app PRIVATE calculator_protocol)
