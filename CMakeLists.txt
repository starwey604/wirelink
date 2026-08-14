cmake_minimum_required(VERSION 3.21)

project(wirelink VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(WIRELINK_RX_BACKEND "BIPBUF" CACHE STRING
    "Wirelink RX ring backend (BIPBUF or LWRB)")
set_property(CACHE WIRELINK_RX_BACKEND PROPERTY STRINGS BIPBUF LWRB)

set(WIRELINK_SOURCES
  src/bipbuf.c
  src/cobs.c
  src/crc.c
  src/frame.c
  src/init.c
  src/protocol.c
  src/transport.c
  src/types.c
)

if(WIRELINK_RX_BACKEND STREQUAL "BIPBUF")
  list(APPEND WIRELINK_SOURCES src/rx_ring_bipbuf.c)
elseif(WIRELINK_RX_BACKEND STREQUAL "LWRB")
  list(APPEND WIRELINK_SOURCES
    src/rx_ring_lwrb.c
    third_party/lwrb/src/lwrb.c
  )
else()
  message(FATAL_ERROR
    "Unknown WIRELINK_RX_BACKEND='${WIRELINK_RX_BACKEND}'; use BIPBUF or LWRB")
endif()

add_library(wirelink ${WIRELINK_SOURCES})

target_include_directories(wirelink PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(WIRELINK_RX_BACKEND STREQUAL "LWRB")
  target_include_directories(wirelink PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lwrb/include
  )
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink PRIVATE -Wall -Wextra -Wpedantic)
endif()
