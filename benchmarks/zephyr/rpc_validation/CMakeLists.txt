# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_rpc_validation_cpu LANGUAGES C)
set(RPC_VALIDATION_CODEC_DIR "" CACHE PATH "Frozen generated validation codec directory")
set(RPC_VALIDATION_CODEGEN_ABI "" CACHE STRING "ABI of the selected generated codec")
if(NOT EXISTS "${RPC_VALIDATION_CODEC_DIR}/validation.c" OR NOT RPC_VALIDATION_CODEGEN_ABI MATCHES "^(28|29)$")
  message(FATAL_ERROR "Provide matching frozen codec directory and ABI (28 baseline / 29 candidate)")
endif()
target_sources(app PRIVATE src/main.c ../../rpc_validation/workload.c
  "${RPC_VALIDATION_CODEC_DIR}/validation.c")
target_include_directories(app PRIVATE ../../rpc_validation "${RPC_VALIDATION_CODEC_DIR}")
target_compile_definitions(app PRIVATE RPC_VALIDATION_ABI=${RPC_VALIDATION_CODEGEN_ABI})
