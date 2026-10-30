# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_static_packet_port_unit)
set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(PROTOTYPE "${WIRELINK_ROOT}/experimental/static_kernel")
target_sources(testbinary PRIVATE
  ${PROTOTYPE}/src/packet_port.c ${PROTOTYPE}/tests/packet_port.c
  ${PROTOTYPE}/src/native_none.c ${PROTOTYPE}/src/rpc_wire.c
  ${PROTOTYPE}/tests/kernel_wire.c ${PROTOTYPE}/src/kernel.c ${PROTOTYPE}/tests/kernel.c)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/include ${PROTOTYPE}/include)
target_compile_definitions(testbinary PRIVATE WL_PACKET_ZTEST=1)
set_property(SOURCE ${PROTOTYPE}/src/packet_port.c ${PROTOTYPE}/src/native_none.c
  ${PROTOTYPE}/src/rpc_wire.c ${PROTOTYPE}/src/kernel.c APPEND PROPERTY
  COMPILE_OPTIONS -std=c11 -Wall -Wextra -Wpedantic)
