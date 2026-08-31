# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_bulk_sender_unit)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

target_sources(testbinary PRIVATE
  src/main.c
  ${WIRELINK_ROOT}/src/bulk_sender.c
)

target_include_directories(testbinary PRIVATE
  ${WIRELINK_ROOT}/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(
    ${WIRELINK_ROOT}/src/bulk_sender.c
    PROPERTIES COMPILE_OPTIONS
      "-Wall;-Wextra;-Wpedantic;-Werror;-Wframe-larger-than=256"
  )
endif()
