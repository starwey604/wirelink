# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate(TARGET device_protocol SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/device.wl"
  PROFILES "${CMAKE_CURRENT_SOURCE_DIR}/services.bind.wl"
    "${CMAKE_CURRENT_SOURCE_DIR}/client.bind.wl" "${CMAKE_CURRENT_SOURCE_DIR}/server.bind.wl")
add_executable(wirelink_device_service_demo client.c server.c device_service.c calculator_service.c self_test.c)
target_include_directories(wirelink_device_service_demo PRIVATE ../common)
target_link_libraries(wirelink_device_service_demo PRIVATE device_protocol Wirelink::packet_loopback)
wl_warnings(wirelink_device_service_demo PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BUILD_TESTING)
  add_test(NAME wirelink_device_service_demo COMMAND wirelink_device_service_demo)
endif()
if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME wirelink_device_service_extension COMMAND ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/tests/tutorials/device_service_extension.py"
    --source "${PROJECT_SOURCE_DIR}" --wlc "${WIRELINK_WLC_EXECUTABLE}"
    --cmake "${CMAKE_COMMAND}" --generator "${CMAKE_GENERATOR}" --c-compiler "${CMAKE_C_COMPILER}")
  set_tests_properties(wirelink_device_service_extension PROPERTIES TIMEOUT 150)
endif()
