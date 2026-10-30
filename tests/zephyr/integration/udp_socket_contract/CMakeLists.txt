# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_udp_socket_contract LANGUAGES C)
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/adapters/zephyr/udp/src)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
