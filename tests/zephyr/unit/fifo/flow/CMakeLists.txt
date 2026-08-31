# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_fifo_command_flow)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../..")

target_sources(testbinary PRIVATE
  src/main.c
  ${WIRELINK_ROOT}/src/fifo.c
)

target_include_directories(testbinary PRIVATE
  ${WIRELINK_ROOT}/include
)

target_link_libraries(testbinary PRIVATE pthread)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(
    ${WIRELINK_ROOT}/src/fifo.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror"
  )
endif()
