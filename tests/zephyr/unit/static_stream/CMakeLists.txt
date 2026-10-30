# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_static_stream_unit)
get_filename_component(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
target_sources(testbinary PRIVATE src/main.c ${WIRELINK_ROOT}/tests/core/packet_stream.c
  ${WIRELINK_ROOT}/src/packet_port.c ${WIRELINK_ROOT}/src/native_none.c
  ${WIRELINK_ROOT}/src/packet_stream.c ${WIRELINK_ROOT}/src/cobs_crc16_stream.c
  ${WIRELINK_ROOT}/src/length16_none_stream.c ${WIRELINK_ROOT}/src/byte_fifo.c
  ${WIRELINK_ROOT}/src/cobs.c ${WIRELINK_ROOT}/src/crc.c
  ${WIRELINK_ROOT}/src/frame.c ${WIRELINK_ROOT}/src/frame_cobs_crc16.c)
target_compile_definitions(testbinary PRIVATE WL_PACKET_ZTEST=1)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/include ${WIRELINK_ROOT}/include)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/usb_bulk/include)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_irq/include)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_dma/include)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_poll/include)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/udp/include)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/runtime/zephyr/include)
