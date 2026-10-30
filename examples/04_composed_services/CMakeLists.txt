# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate(TARGET composed_protocol SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/product.wl"
  PROFILES "${CMAKE_CURRENT_SOURCE_DIR}/arm.bind.wl" "${CMAKE_CURRENT_SOURCE_DIR}/upgrade.bind.wl")
add_executable(wirelink_composed_services main.c)
target_include_directories(wirelink_composed_services PRIVATE ../common)
target_link_libraries(wirelink_composed_services PRIVATE composed_protocol Wirelink::packet_loopback Wirelink::framing_primitives)
wl_warnings(wirelink_composed_services PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BUILD_TESTING)
  add_test(NAME wirelink_composed_services COMMAND wirelink_composed_services)
endif()

if(BUILD_TESTING)
  get_target_property(_compiler composed_protocol WIRELINK_WLC_EXECUTABLE)
  add_test(NAME wirelink_import_dependencies COMMAND "${CMAKE_COMMAND}"
    "-DWIRELINK_SOURCE_DIR=${PROJECT_SOURCE_DIR}" "-DMATCHING_WLC=${_compiler}"
    "-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/import-dependencies"
    -P "${PROJECT_SOURCE_DIR}/tests/package/import_dependencies.cmake")
  set_tests_properties(wirelink_import_dependencies PROPERTIES TIMEOUT 90)
endif()
