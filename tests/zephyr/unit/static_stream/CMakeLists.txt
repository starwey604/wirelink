# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_static_stream_unit)
get_filename_component(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(PROTOTYPE ${WIRELINK_ROOT}/experimental/static_kernel)
target_sources(testbinary PRIVATE src/main.c ${PROTOTYPE}/tests/packet_stream.c
  ${PROTOTYPE}/src/packet_port.c ${PROTOTYPE}/src/native_none.c
  ${PROTOTYPE}/src/packet_stream.c ${PROTOTYPE}/src/cobs_crc16_stream.c
  ${PROTOTYPE}/src/length16_none_stream.c ${PROTOTYPE}/src/byte_fifo.c
  ${WIRELINK_ROOT}/src/cobs.c ${WIRELINK_ROOT}/src/crc.c
  ${WIRELINK_ROOT}/src/frame.c ${WIRELINK_ROOT}/src/frame_cobs_crc16.c)
target_compile_definitions(testbinary PRIVATE WL_PACKET_ZTEST=1)
target_include_directories(testbinary PRIVATE ${WIRELINK_ROOT}/include ${PROTOTYPE}/include)
