# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)
set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_composed_services_hil)
include(${WIRELINK_ROOT}/cmake/WirelinkWlc.cmake)
set(COMPOSED_ROOT "${WIRELINK_ROOT}/examples/04_composed_services")
wirelink_wlc_generate_codec(TARGET composed_codec SCHEMA "${COMPOSED_ROOT}/product.wl")
wirelink_wlc_generate_runtime(TARGET composed_protocol CODEC_TARGET composed_codec
  PROFILES "${COMPOSED_ROOT}/arm.bind.wl" "${COMPOSED_ROOT}/upgrade.bind.wl"
  RUNTIME_NAME product)
target_link_libraries(composed_codec PUBLIC zephyr_interface)
target_sources(app PRIVATE "${COMPOSED_ROOT}/main.c" ${WIRELINK_ROOT}/adapters/loopback/src/loopback.c)
target_link_libraries(app PRIVATE composed_protocol)
