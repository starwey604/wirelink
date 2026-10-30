# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28.0)
set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_udp_validation LANGUAGES C)
include(${WIRELINK_ROOT}/cmake/WirelinkWlc.cmake)
set(UDP_DEMO_SCHEMA "${WIRELINK_ROOT}/examples/05_zephyr_udp")
wirelink_wlc_generate(TARGET udp_demo_protocol SCHEMA "${UDP_DEMO_SCHEMA}/udp_demo.wl"
  PROFILE "${UDP_DEMO_SCHEMA}/udp_demo.bind.wl")
target_include_directories(app PRIVATE "${UDP_DEMO_SCHEMA}")
target_link_libraries(app PRIVATE udp_demo_protocol)
target_sources(app PRIVATE src/main.c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
