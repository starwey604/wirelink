# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(WIRELINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
list(APPEND ZEPHYR_EXTRA_MODULES ${WIRELINK_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_frame_integration)

# Reuse the pure-core cases against target code generation and libc, especially
# fixed-size header copies at unaligned addresses and overlapping payloads.
target_sources(app PRIVATE ../../unit/frame/src/main.c)
target_include_directories(app PRIVATE ${WIRELINK_ROOT}/include)
