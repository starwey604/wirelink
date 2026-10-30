# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)
set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_endpoint_clock)
include(${WIRELINK_ROOT}/cmake/WirelinkWlc.cmake)
wirelink_wlc_generate(TARGET calculator_protocol SCHEMA "${WIRELINK_ROOT}/examples/01_rpc/calculator.wl"
  PROFILE "${WIRELINK_ROOT}/examples/01_rpc/calculator.bind.wl")
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE "${WIRELINK_ROOT}/examples/common")
target_link_libraries(app PRIVATE calculator_protocol)
