# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_static_packet_port_unit)
set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
target_sources(testbinary PRIVATE
  ${WIRELINK_ROOT}/src/packet_port.c ${WIRELINK_ROOT}/tests/core/packet_port.c
  ${WIRELINK_ROOT}/src/native_none.c ${WIRELINK_ROOT}/src/rpc_wire.c
  ${WIRELINK_ROOT}/tests/core/kernel_wire.c ${WIRELINK_ROOT}/src/kernel.c ${WIRELINK_ROOT}/tests/core/kernel.c)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/include ${WIRELINK_ROOT}/include)
target_compile_definitions(testbinary PRIVATE WL_PACKET_ZTEST=1)
set_property(SOURCE ${WIRELINK_ROOT}/src/packet_port.c ${WIRELINK_ROOT}/src/native_none.c
  ${WIRELINK_ROOT}/src/rpc_wire.c ${WIRELINK_ROOT}/src/kernel.c APPEND PROPERTY
  COMPILE_OPTIONS -std=c11 -Wall -Wextra -Wpedantic)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/usb_bulk/include)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_irq/include)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_dma/include)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_poll/include)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/udp/include)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/runtime/zephyr/include)
