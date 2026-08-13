# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(wirelink)

zephyr_library_sources_ifdef(CONFIG_WIRELINK
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/bipbuf.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/types.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/src/cobs.c
)

zephyr_library_include_directories(
  ${ZEPHYR_CURRENT_MODULE_DIR}/include
)
