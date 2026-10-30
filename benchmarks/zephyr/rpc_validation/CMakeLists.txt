# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
list(APPEND ZEPHYR_EXTRA_MODULES "${CMAKE_CURRENT_LIST_DIR}/../../..")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_rpc_validation_cpu LANGUAGES C)
if(RPC_VALIDATION_OPTIMIZED_CODEC)
  message(FATAL_ERROR "Private codec helper experiment retired; use public codec validation v3")
endif()
include(../../../cmake/WirelinkWlc.cmake)
wirelink_wlc_generate_codec(TARGET validation_codec
  SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/../../rpc_validation/validation.wl")
target_sources(app PRIVATE src/main.c ../../rpc_validation/workload.c)
target_include_directories(app PRIVATE ../../rpc_validation)
target_link_libraries(app PRIVATE validation_codec)
