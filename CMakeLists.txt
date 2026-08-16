cmake_minimum_required(VERSION 3.21)

project(wirelink VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(WIRELINK_SOURCES
  src/bipbuf.c
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
