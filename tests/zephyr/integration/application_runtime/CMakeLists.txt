# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_application_runtime_integration)

set(WLC_GENERATED_DIR
    "${WIRELINK_ROOT}/tests/fixtures/wlc/generated/current")

target_sources(app PRIVATE
  src/main.c
  ${WLC_GENERATED_DIR}/control.c
  ${WLC_GENERATED_DIR}/control_bindings.c
  ${WLC_GENERATED_DIR}/control_runtime.c
)

target_include_directories(app PRIVATE
  ${WIRELINK_ROOT}/include
  ${WLC_GENERATED_DIR}
)

target_compile_features(app PRIVATE c_std_11)
