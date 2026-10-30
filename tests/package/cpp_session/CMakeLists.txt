# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.21)
project(wirelink_installed_cpp_session LANGUAGES C CXX)
include(CTest)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
find_package(Wirelink CONFIG REQUIRED COMPONENTS cpp_host)
set(WIRELINK_WLC_EXECUTABLE "" CACHE FILEPATH "Matching WLC")
if(NOT EXISTS "${WIRELINK_WLC_EXECUTABLE}")
  message(FATAL_ERROR "Set WIRELINK_WLC_EXECUTABLE")
endif()
get_filename_component(_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(_fixture ${_root}/tests/package/static_kernel)
set(_generated ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(OUTPUT ${_generated}/probe.c ${_generated}/probe_kernel.c
    ${_generated}/probe.h ${_generated}/probe_values.h ${_generated}/probe_kernel.h
    ${_generated}/probe_kernel_codecs.h
  COMMAND ${WIRELINK_WLC_EXECUTABLE} compile-kernel ${_fixture}/probe.wl
    --profile ${_fixture}/probe.bind.wl --out-dir ${_generated}
  DEPENDS ${WIRELINK_WLC_EXECUTABLE} ${_fixture}/probe.wl ${_fixture}/probe.bind.wl VERBATIM)
add_executable(installed_cpp_session ${_root}/bindings/cpp/tests/session.cpp
  ${_root}/bindings/cpp/tests/endpoint.c ${_generated}/probe.c ${_generated}/probe_kernel.c)
target_include_directories(installed_cpp_session PRIVATE ${_generated})
target_link_libraries(installed_cpp_session PRIVATE Wirelink::cpp_host Wirelink::asio_udp)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(installed_cpp_session PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME installed_cpp_session COMMAND installed_cpp_session)
set_tests_properties(installed_cpp_session PROPERTIES TIMEOUT 30)
