# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_bulk_receiver_unit)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(BULK_RECEIVER_SOURCE "${WIRELINK_ROOT}/src/bulk_receiver.c")

target_sources(testbinary PRIVATE
  src/main.c
  ${BULK_RECEIVER_SOURCE}
)

target_include_directories(testbinary PRIVATE
  ${WIRELINK_ROOT}/include
)

target_compile_definitions(testbinary PRIVATE WL_BULK_TEST_HOOKS=1)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  set_property(SOURCE ${BULK_RECEIVER_SOURCE} APPEND PROPERTY COMPILE_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Werror=frame-larger-than=256
  )
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
  set_property(SOURCE ${BULK_RECEIVER_SOURCE} APPEND PROPERTY COMPILE_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wframe-larger-than=256
    -Werror=frame-larger-than
  )
endif()
