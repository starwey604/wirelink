# SPDX-License-Identifier: Apache-2.0
set(ASTRIAL_BUILD_USB ON CACHE BOOL "" FORCE)
set(ASTRIAL_ENABLE_IPO OFF CACHE BOOL "" FORCE)
add_subdirectory("${WIRELINK_TEST_ASTRIAL_SOURCE_DIR}" astrial EXCLUDE_FROM_ALL)
find_package(Wirelink CONFIG REQUIRED COMPONENTS astrial astrial_usb)
set(core_tests "${CMAKE_CURRENT_SOURCE_DIR}/../../core")
consumer(installed_usb "${core_tests}/packet_usb.cpp" Wirelink::astrial_usb Wirelink::endpoint)
if(UNIX)
  consumer(installed_serial "${core_tests}/packet_serial.cpp" Wirelink::astrial Wirelink::endpoint)
  target_sources(installed_serial PRIVATE "${core_tests}/packet_serial_owner.c")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(installed_serial PRIVATE util)
  endif()
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME installed_component_diagnostics COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/check_components.py" "${CMAKE_COMMAND}"
  "${CMAKE_PREFIX_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/negative")
