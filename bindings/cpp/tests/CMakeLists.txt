# SPDX-License-Identifier: Apache-2.0
if(NOT WIRELINK_WLC_EXECUTABLE)
  return()
endif()
set(_generated ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(_fixture ${PROJECT_SOURCE_DIR}/tests/package/static_kernel)
add_custom_command(OUTPUT ${_generated}/probe.c ${_generated}/probe_kernel.c
    ${_generated}/probe.h ${_generated}/probe_values.h ${_generated}/probe_kernel.h
    ${_generated}/probe_kernel_codecs.h
  COMMAND ${WIRELINK_WLC_EXECUTABLE} compile-kernel ${_fixture}/probe.wl
    --profile ${_fixture}/probe.bind.wl --out-dir ${_generated}
  DEPENDS ${WIRELINK_WLC_EXECUTABLE} ${_fixture}/probe.wl ${_fixture}/probe.bind.wl VERBATIM)
add_executable(wirelink_cpp_session_tests session.cpp endpoint.c
  ${_generated}/probe.c ${_generated}/probe_kernel.c)
target_include_directories(wirelink_cpp_session_tests PRIVATE ${_generated})
target_link_libraries(wirelink_cpp_session_tests PRIVATE wirelink_cpp_host wirelink_asio_udp)
wl_warnings(wirelink_cpp_session_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME wirelink_cpp_session COMMAND wirelink_cpp_session_tests)
set_tests_properties(wirelink_cpp_session PROPERTIES TIMEOUT 30)
