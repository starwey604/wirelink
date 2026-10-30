# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)
set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_rpc_platform)
include(${WIRELINK_ROOT}/cmake/WirelinkWlc.cmake)
if(DEFINED WIRELINK_RPC_TEST_CAPACITY)
  message(FATAL_ERROR "The current kernel has four fixed call slots; the one-slot capacity knob was removed")
endif()
wirelink_wlc_generate(TARGET validation_protocol SCHEMA "${CMAKE_CURRENT_LIST_DIR}/../rpc_usability/rpc_validation.wl"
  PROFILE "${CMAKE_CURRENT_LIST_DIR}/../rpc_usability/rpc_validation.bind.wl")
target_sources(app PRIVATE src/main.c)
target_link_libraries(app PRIVATE validation_protocol)
