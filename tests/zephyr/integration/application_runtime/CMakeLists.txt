# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_application_runtime_integration)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(WLC_GENERATED_DIR
    "${WIRELINK_ROOT}/tests/fixtures/wlc/generated/current")

target_sources(app PRIVATE
  src/main.c
  ${WIRELINK_ROOT}/src/cobs.c
  ${WIRELINK_ROOT}/src/crc.c
  ${WIRELINK_ROOT}/src/frame.c
  ${WIRELINK_ROOT}/src/init.c
  ${WIRELINK_ROOT}/src/latest.c
  ${WIRELINK_ROOT}/src/protocol.c
  ${WIRELINK_ROOT}/src/rpc.c
  ${WIRELINK_ROOT}/src/rx_ring_bipbuf.c
  ${WIRELINK_ROOT}/src/schedule.c
  ${WIRELINK_ROOT}/src/transport.c
  ${WIRELINK_ROOT}/src/types.c
  ${WIRELINK_ROOT}/src/unit_rx.c
  ${WLC_GENERATED_DIR}/control.c
  ${WLC_GENERATED_DIR}/control_bindings.c
)

target_include_directories(app PRIVATE
  ${WIRELINK_ROOT}/include
  ${WLC_GENERATED_DIR}
)

target_compile_features(app PRIVATE c_std_11)
