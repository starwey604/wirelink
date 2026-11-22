# SPDX-License-Identifier: Apache-2.0
zephyr_library_named(wirelink)
set(_wl ${ZEPHYR_CURRENT_MODULE_DIR})
zephyr_include_directories(${_wl}/include ${_wl}/runtime/zephyr/include
  ${_wl}/adapters/zephyr/uart_dma/include ${_wl}/adapters/zephyr/uart_irq/include
  ${_wl}/adapters/zephyr/uart_poll/include ${_wl}/adapters/zephyr/usb_bulk/include
  ${_wl}/adapters/zephyr/udp/include)
if(CONFIG_WIRELINK)
  set(_core ${_wl}/src/node.c ${_wl}/src/endpoint.c ${_wl}/src/packet_port.c ${_wl}/src/types.c ${_wl}/src/codec.c)
  zephyr_library_sources(${_core})
  zephyr_library_compile_options(-std=c11)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_property(SOURCE ${_core} APPEND PROPERTY COMPILE_OPTIONS -Wall -Wextra -Wpedantic)
  endif()
endif()
zephyr_library_sources_ifdef(CONFIG_WIRELINK_PACKET_LOOPBACK ${_wl}/src/packet_loopback.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_PACKET_STREAM ${_wl}/src/packet_stream.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_STREAM_COBS_CRC16 ${_wl}/src/cobs_crc16_stream.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_STREAM_LENGTH16_NONE ${_wl}/src/length16_none_stream.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_FRAMING_PRIMITIVES ${_wl}/src/cobs.c ${_wl}/src/crc.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_BYTE_FIFO ${_wl}/src/byte_fifo.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_BYTE_FIFO_COBS ${_wl}/src/byte_fifo_cobs.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_PLATFORM ${_wl}/runtime/zephyr/src/environment.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_WAIT ${_wl}/runtime/zephyr/src/wait.c ${_wl}/runtime/zephyr/src/timeout.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_UART_DMA ${_wl}/adapters/zephyr/uart_dma/src/uart_dma.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_UART_IRQ ${_wl}/adapters/zephyr/uart_irq/src/uart_irq.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_UART_POLL_EXPERIMENTAL ${_wl}/adapters/zephyr/uart_poll/src/uart_poll.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_USB_BULK ${_wl}/adapters/zephyr/usb_bulk/src/usb_bulk.c)
zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_UDP
  ${_wl}/adapters/zephyr/udp/src/udp.c ${_wl}/adapters/zephyr/udp/src/udp_socket.c)
# Preserve the existing platform IRQ callback mapping, without moving kernel,
# framing, codecs or owner execution into IRAM.
if(CONFIG_WIRELINK_ZEPHYR_UART_IRQ AND CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
  set_property(SOURCE ${_wl}/adapters/zephyr/uart_irq/src/uart_irq.c APPEND PROPERTY
    COMPILE_OPTIONS -include ${_wl}/adapters/zephyr/uart_irq/platform/esp32_attributes.h)
endif()
