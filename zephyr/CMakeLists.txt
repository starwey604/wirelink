# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(wirelink)

include(${ZEPHYR_CURRENT_MODULE_DIR}/cmake/WirelinkLinkProfile.cmake)
if(CONFIG_WIRELINK_LINK_PROFILE_COBS_CRC16)
  wirelink_target_link_profile(wirelink cobs-crc16)
else()
  wirelink_target_link_profile(wirelink multi)
endif()

zephyr_library_sources_ifdef(CONFIG_WIRELINK_PLATFORM
  ${ZEPHYR_CURRENT_MODULE_DIR}/runtime/zephyr/src/environment.c
)

zephyr_library_sources_ifdef(CONFIG_WIRELINK
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/bulk_receiver.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/bulk_sender.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/endpoint.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/frame_cobs_crc16.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/fifo.c
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
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/frame.c ${ZEPHYR_CURRENT_MODULE_DIR}/src/frame_cobs_crc16.c ${ZEPHYR_CURRENT_MODULE_DIR}/src/unit_frame.c APPEND
      PROPERTY COMPILE_OPTIONS -Werror=frame-larger-than=256)
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/protocol.c APPEND
      PROPERTY COMPILE_OPTIONS -Werror=frame-larger-than=256)
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/bulk_receiver.c APPEND
      PROPERTY COMPILE_OPTIONS -Werror=frame-larger-than=256)
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/bulk_sender.c APPEND
      PROPERTY COMPILE_OPTIONS -Werror=frame-larger-than=256)
  elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set_property(SOURCE ${ZEPHYR_CURRENT_MODULE_DIR}/src/frame.c ${ZEPHYR_CURRENT_MODULE_DIR}/src/frame_cobs_crc16.c ${ZEPHYR_CURRENT_MODULE_DIR}/src/unit_frame.c APPEND
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

zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_UART_POLL_EXPERIMENTAL
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/uart_poll/src/uart_poll.c
)

if(CONFIG_WIRELINK_ZEPHYR_UART_IRQ AND CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
  set_property(SOURCE
    ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/uart_irq/src/uart_irq.c
    APPEND PROPERTY COMPILE_OPTIONS -include
    ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/uart_irq/platform/esp32_attributes.h)
endif()

if(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32 AND
   (CONFIG_WIRELINK_ZEPHYR_UART_IRQ OR CONFIG_WIRELINK_ZEPHYR_UART_POLL_EXPERIMENTAL))
  zephyr_linker_sources(RAMFUNC_SECTION
    ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/uart_irq/owner_iram.ld)
endif()

zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_UDP
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/udp/src/socket_contract.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/udp/src/udp.c
)

zephyr_include_directories(
  ${ZEPHYR_CURRENT_MODULE_DIR}/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/common/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/uart_dma/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/usb_bulk/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/uart_irq/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/uart_poll/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/adapters/zephyr/udp/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/runtime/zephyr/include
  ${ZEPHYR_CURRENT_MODULE_DIR}/runtime/storage/include
)

zephyr_library_sources_ifdef(CONFIG_WIRELINK_ZEPHYR_WAIT
  ${ZEPHYR_CURRENT_MODULE_DIR}/runtime/zephyr/src/wait.c
)

zephyr_library_sources_ifdef(CONFIG_WIRELINK
  ${ZEPHYR_CURRENT_MODULE_DIR}/runtime/zephyr/src/timeout.c
)

zephyr_library_sources_ifdef(CONFIG_WIRELINK_STORAGE
  ${ZEPHYR_CURRENT_MODULE_DIR}/runtime/storage/src/fixed_pool.c
)

if(CONFIG_WIRELINK)
  # Optional wire modes and standalone exact/alias tools follow the common
  # core owner, so their capability-specific size cannot split that owner.
  add_library(wirelink_framing_internal STATIC

    ${ZEPHYR_CURRENT_MODULE_DIR}/src/frame.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/init.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/cobs.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/crc.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/unit_frame.c)
  if(CONFIG_XTENSA AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_property(SOURCE
      ${ZEPHYR_CURRENT_MODULE_DIR}/src/frame_cobs_crc16.c
      ${ZEPHYR_CURRENT_MODULE_DIR}/src/protocol.c
      ${ZEPHYR_CURRENT_MODULE_DIR}/src/transport.c
      ${ZEPHYR_CURRENT_MODULE_DIR}/src/unit_rx.c
      APPEND PROPERTY COMPILE_OPTIONS -mtext-section-literals)
    # Keep each literal pool with its function; a profile's pools must not be
    # inserted ahead of unrelated core/runtime text by the global .literal.*
    # wildcard. This changes placement, not -Os, LTO, cache size or IRAM.
    target_compile_options(wirelink_framing_internal PRIVATE -mtext-section-literals)
  endif()
  target_link_libraries(wirelink_framing_internal PRIVATE zephyr_interface)
  target_include_directories(wirelink_framing_internal PRIVATE ${ZEPHYR_CURRENT_MODULE_DIR}/include)
  if(CONFIG_WIRELINK_LINK_PROFILE_COBS_CRC16)
    wirelink_target_link_profile(wirelink_framing_internal cobs-crc16)
  else()
    wirelink_target_link_profile(wirelink_framing_internal multi)
  endif()
  # Extract in source order; --gc-sections still discards unused functions.
  # Ordinary archive demand order otherwise moves the common kernels when
  # optional mode symbols change the unresolved-symbol queue.
  add_dependencies(wirelink wirelink_framing_internal)
  # Keep the ld scope and its archive in one argument: CMake otherwise places
  # transitive archive targets after the two separate link-flag arguments.
  target_link_libraries(wirelink PUBLIC
    "-Wl,--whole-archive,$<TARGET_FILE:wirelink_framing_internal>,--no-whole-archive")
  set_property(TARGET wirelink APPEND PROPERTY INTERFACE_LINK_DEPENDS
    "$<TARGET_FILE:wirelink_framing_internal>")
endif()
