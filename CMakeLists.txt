cmake_minimum_required(VERSION 3.21)

project(wirelink VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

add_library(wirelink
  src/bipbuf.c
  src/types.c
)

target_include_directories(wirelink PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
