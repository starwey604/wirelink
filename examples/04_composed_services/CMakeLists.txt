# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate_codec(TARGET composed_codec SCHEMA product.wl)
wirelink_wlc_generate_runtime(TARGET composed_protocol CODEC_TARGET composed_codec
  PROFILES arm.bind.wl upgrade.bind.wl RUNTIME_NAME product)
add_executable(wirelink_composed_services main.c)
target_link_libraries(wirelink_composed_services PRIVATE composed_protocol Wirelink::loopback)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_composed_services PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(BUILD_TESTING)
  get_target_property(_compiler composed_codec WIRELINK_WLC_EXECUTABLE)
  add_test(NAME wirelink_composed_services COMMAND wirelink_composed_services)
  add_test(NAME wirelink_import_dependencies COMMAND "${CMAKE_COMMAND}"
    "-DWIRELINK_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DMATCHING_WLC=${_compiler}"
    "-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/import-dependencies"
    -P "${PROJECT_SOURCE_DIR}/tests/package/import_dependencies.cmake")
endif()
