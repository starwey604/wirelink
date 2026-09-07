# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(wirelink)

zephyr_library_sources_ifdef(CONFIG_WIRELINK
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/bulk_receiver.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/bulk_sender.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/cobs.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/crc.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/endpoint.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/fifo.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/frame.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/init.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/latest.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/pump.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/protocol.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/rpc.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/rpc_async.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/rx_ring_bipbuf.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/schedule.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/transport.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/types.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/unit_rx.c
)

if(CONFIG_WIRELINK AND NOT CONFIG_ASAN AND NOT CONFIG_UBSAN)
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/frame.c APPEND
      PROPERTY COMPILE_OPTIONS -Werror=frame-larger-than=256)
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/protocol.c APPEND
      PROPERTY COMPILE_OPTIONS -Werror=frame-larger-than=256)
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/bulk_receiver.c APPEND
      PROPERTY COMPILE_OPTIONS -Werror=frame-larger-than=256)
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/bulk_sender.c APPEND
      PROPERTY COMPILE_OPTIONS -Werror=frame-larger-than=256)
  elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/frame.c APPEND
      PROPERTY COMPILE_OPTIONS
        -Wframe-larger-than=256
        -Werror=frame-larger-than)
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/protocol.c APPEND
      PROPERTY COMPILE_OPTIONS
        -Wframe-larger-than=256
        -Werror=frame-larger-than)
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/bulk_receiver.c APPEND
      PROPERTY COMPILE_OPTIONS
        -Wframe-larger-than=256
        -Werror=frame-larger-than)
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/bulk_sender.c APPEND
      PROPERTY COMPILE_OPTIONS
        -Wframe-larger-than=256
        -Werror=frame-larger-than)
  endif()
endif()

zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_UART_DMA
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/uart_dma/src/uart_dma.c
)

zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_USB_BULK
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/usb_bulk/src/usb_bulk.c
)

zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_UART_IRQ
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/uart_irq/src/uart_irq.c
)

zephyr_include_directories(
  ${ZEPHYR_CURRENT_MODULE_DIR}/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/uart_dma/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/usb_bulk/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/uart_irq/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/runtime/zephyr/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/runtime/storage/include
)

zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_WAIT
  ${ZEPHYR_CURRENT_MODULE_DIR}/runtime/zephyr/src/wait.c
)

zephyr_library_sources_ifdef(CONFIG_WIRELINK_STORAGE
  ${ZEPHYR_CURRENT_MODULE_DIR}/runtime/storage/src/fixed_pool.c
)
