# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)
get_filename_component(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_composed_services)
include(${WIRELINK_ROOT}/cmake/WirelinkWlc.cmake)
set(COMPOSED_ROOT "${WIRELINK_ROOT}/examples/04_composed_services")
wirelink_wlc_generate(TARGET composed_protocol SCHEMA "${COMPOSED_ROOT}/product.wl"
  PROFILES "${COMPOSED_ROOT}/arm.bind.wl" "${COMPOSED_ROOT}/upgrade.bind.wl")
target_sources(app PRIVATE "${COMPOSED_ROOT}/main.c")
target_include_directories(app PRIVATE "${WIRELINK_ROOT}/examples/common")
target_link_libraries(app PRIVATE composed_protocol)
