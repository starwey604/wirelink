# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_static_uart)
target_sources(app PRIVATE src/irq.c src/dma.c src/poll.c
  ${WIRELINK_ROOT}/adapters/zephyr/uart_poll/src/kernel_uart_poll.c
  ${WIRELINK_ROOT}/adapters/zephyr/uart_irq/src/kernel_uart_irq.c ${WIRELINK_ROOT}/adapters/zephyr/uart_dma/src/kernel_uart_dma.c
  ${WIRELINK_ROOT}/src/packet_port.c ${WIRELINK_ROOT}/src/native_none.c
  ${WIRELINK_ROOT}/src/packet_stream.c ${WIRELINK_ROOT}/src/cobs_crc16_stream.c
  ${WIRELINK_ROOT}/src/byte_fifo.c ${WIRELINK_ROOT}/src/byte_fifo_cobs.c
  ${WIRELINK_ROOT}/src/cobs.c ${WIRELINK_ROOT}/src/crc.c)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/include ${WIRELINK_ROOT}/include
  ${WIRELINK_ROOT}/runtime/zephyr/include)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/usb_bulk/include)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_irq/include)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_dma/include)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/uart_poll/include)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/udp/include)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/runtime/zephyr/include)
