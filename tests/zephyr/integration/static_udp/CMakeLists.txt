# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_static_udp)
target_sources(app PRIVATE src/main.c ${WIRELINK_ROOT}/src/packet_port.c
  ${WIRELINK_ROOT}/src/packet_stream.c ${WIRELINK_ROOT}/src/cobs_crc16_stream.c ${WIRELINK_ROOT}/src/native_none.c
  ${WIRELINK_ROOT}/adapters/zephyr/udp/src/kernel_udp.c ${WIRELINK_ROOT}/adapters/zephyr/udp/src/kernel_udp_socket.c
  ${WIRELINK_ROOT}/src/cobs.c ${WIRELINK_ROOT}/src/crc.c
  ${WIRELINK_ROOT}/src/frame.c ${WIRELINK_ROOT}/src/frame_cobs_crc16.c)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/include ${WIRELINK_ROOT}/include
  ${WIRELINK_ROOT}/adapters/zephyr/udp/src ${WIRELINK_ROOT}/runtime/zephyr/include)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/usb_bulk/include)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_irq/include)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_dma/include)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_poll/include)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/udp/include)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/runtime/zephyr/include)
