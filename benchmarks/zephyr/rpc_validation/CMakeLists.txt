# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_rpc_validation_cpu LANGUAGES C)
set(RPC_VALIDATION_CODEC_DIR "" CACHE PATH "Frozen generated validation codec directory")
set(RPC_VALIDATION_OPTIMIZED_CODEC "" CACHE STRING "1 when the frozen codec exposes the optimized fingerprint seam")
if(NOT EXISTS "${RPC_VALIDATION_CODEC_DIR}/validation.c" OR NOT RPC_VALIDATION_OPTIMIZED_CODEC MATCHES "^(0|1)$")
  message(FATAL_ERROR "Provide a frozen codec directory and RPC_VALIDATION_OPTIMIZED_CODEC (0 baseline / 1 optimized)")
endif()
target_sources(app PRIVATE src/main.c ../../rpc_validation/workload.c
  "${RPC_VALIDATION_CODEC_DIR}/validation.c")
target_include_directories(app PRIVATE ../../rpc_validation "${RPC_VALIDATION_CODEC_DIR}")
target_compile_definitions(app PRIVATE RPC_VALIDATION_OPTIMIZED_CODEC=${RPC_VALIDATION_OPTIMIZED_CODEC})
