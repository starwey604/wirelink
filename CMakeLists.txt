cmake_minimum_required(VERSION 3.21)

project(wirelink VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(WIRELINK_SOURCES
  src/cobs.c
  src/crc.c
  src/frame.c
  src/init.c
  src/protocol.c
  src/rx_ring_bipbuf.c
  src/transport.c
  src/types.c
)

add_library(wirelink ${WIRELINK_SOURCES})

target_include_directories(wirelink PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(WIRELINK_BUILD_ASTRIAL_ADAPTER
  "Build the optional Astrial desktop serial adapter" OFF)
option(WIRELINK_BUILD_FUZZERS
  "Build Clang libFuzzer targets" OFF)
set(WIRELINK_ASTRIAL_SOURCE_DIR "" CACHE PATH
  "Astrial source tree used when no astrial target is already available")

if(WIRELINK_BUILD_ASTRIAL_ADAPTER)
  if(NOT TARGET astrial)
    if(WIRELINK_ASTRIAL_SOURCE_DIR STREQUAL "")
      message(FATAL_ERROR
        "WIRELINK_BUILD_ASTRIAL_ADAPTER requires an existing astrial target "
        "or WIRELINK_ASTRIAL_SOURCE_DIR")
    endif()
    set(_WIRELINK_SAVED_BUILD_EXAMPLE "${BUILD_EXAMPLE}")
    set(_WIRELINK_SAVED_BUILD_TESTING "${BUILD_TESTING}")
    set(BUILD_EXAMPLE OFF)
    set(BUILD_TESTING OFF)
    add_subdirectory("${WIRELINK_ASTRIAL_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/_deps/astrial" EXCLUDE_FROM_ALL)
    set(BUILD_EXAMPLE "${_WIRELINK_SAVED_BUILD_EXAMPLE}")
    set(BUILD_TESTING "${_WIRELINK_SAVED_BUILD_TESTING}")
    unset(_WIRELINK_SAVED_BUILD_EXAMPLE)
    unset(_WIRELINK_SAVED_BUILD_TESTING)
  endif()
  add_subdirectory(adapters/astrial)
endif()

if(WIRELINK_BUILD_FUZZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "WIRELINK_BUILD_FUZZERS requires Clang")
  endif()
  target_compile_options(wirelink PRIVATE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer)
  add_subdirectory(fuzz)
endif()
