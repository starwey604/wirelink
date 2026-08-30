# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_generated_codec_unit)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(WLC_FIXTURE "${WIRELINK_ROOT}/tests/fixtures/wlc/generated")

if(WLC_SCHEMA_REVISION STREQUAL "current")
  set(WLC_GENERATED_DIR "${WLC_FIXTURE}/current")
  target_compile_definitions(testbinary PRIVATE WLC_SCHEMA_CURRENT=1)
elseif(WLC_SCHEMA_REVISION STREQUAL "previous")
  set(WLC_GENERATED_DIR "${WLC_FIXTURE}/previous")
else()
  message(FATAL_ERROR "WLC_SCHEMA_REVISION must be current or previous")
endif()

target_sources(testbinary PRIVATE
  src/main.c
  ${WLC_GENERATED_DIR}/control.c
)

target_include_directories(testbinary PRIVATE
  ${WIRELINK_ROOT}/include
  ${WLC_GENERATED_DIR}
)
