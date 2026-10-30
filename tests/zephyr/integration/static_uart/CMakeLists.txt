# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_static_uart)
set(PROTOTYPE ${WIRELINK_ROOT}/experimental/static_kernel)
target_sources(app PRIVATE src/irq.c src/dma.c src/poll.c
  ${PROTOTYPE}/adapters/zephyr/kernel_uart_poll.c
  ${PROTOTYPE}/adapters/zephyr/kernel_uart_irq.c ${PROTOTYPE}/adapters/zephyr/kernel_uart_dma.c
  ${PROTOTYPE}/src/packet_port.c ${PROTOTYPE}/src/native_none.c
  ${PROTOTYPE}/src/packet_stream.c ${PROTOTYPE}/src/cobs_crc16_stream.c
  ${PROTOTYPE}/src/byte_fifo.c ${PROTOTYPE}/src/byte_fifo_cobs.c
  ${WIRELINK_ROOT}/src/cobs.c ${WIRELINK_ROOT}/src/crc.c)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/include ${PROTOTYPE}/include
  ${PROTOTYPE}/adapters/zephyr/include)
