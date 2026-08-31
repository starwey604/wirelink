# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_latest_concurrent)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../..")

target_sources(app PRIVATE
  src/main.c
  ${WIRELINK_ROOT}/src/latest.c
)

target_include_directories(app PRIVATE
  ${WIRELINK_ROOT}/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(
    ${WIRELINK_ROOT}/src/latest.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror"
  )
endif()
