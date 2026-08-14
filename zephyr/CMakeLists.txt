# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(wirelink)

zephyr_library_sources_ifdef(CONFIG_WIRELINK
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/bipbuf.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/cobs.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/crc.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/frame.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/init.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/protocol.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/transport.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/types.c
)

zephyr_library_sources_ifdef(CONFIG_WIRELINK_RX_BACKEND_BIPBUF
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/rx_ring_bipbuf.c
)

zephyr_library_sources_ifdef(CONFIG_WIRELINK_RX_BACKEND_LWRB
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/rx_ring_lwrb.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/third_party/lwrb/src/lwrb.c
)

zephyr_library_include_directories(
  ${ZEPHYR_CURRENT_MODULE_DIR}/include
)

zephyr_library_include_directories_ifdef(CONFIG_WIRELINK_RX_BACKEND_LWRB
  ${ZEPHYR_CURRENT_MODULE_DIR}/third_party/lwrb/include
)
