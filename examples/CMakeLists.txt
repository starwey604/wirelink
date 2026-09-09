# SPDX-License-Identifier: Apache-2.0

add_executable(wirelink_bare_metal_loopback bare_metal_loopback.c)
target_link_libraries(wirelink_bare_metal_loopback PRIVATE Wirelink::loopback)

if(WIRELINK_BUILD_GETTING_STARTED)
  add_library(wirelink_tutorial_host STATIC common/tutorial_host.cpp)
  target_include_directories(wirelink_tutorial_host PUBLIC common)
  target_link_libraries(wirelink_tutorial_host PUBLIC Wirelink::asio_udp Wirelink::platform)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wirelink_tutorial_host PUBLIC -Wall -Wextra -Wpedantic)
  endif()
  add_subdirectory(00_telemetry)
  add_subdirectory(01_rpc)
  add_subdirectory(02_device_info)
  add_subdirectory(03_device_service)
  if(BUILD_TESTING)
    add_subdirectory(../tests/session ${CMAKE_CURRENT_BINARY_DIR}/session)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME wirelink_tutorial_sources
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tests/tutorials/doc_sources.py)
    add_subdirectory(../tests/clock_bridge ${CMAKE_CURRENT_BINARY_DIR}/clock_bridge)
    add_test(NAME wirelink_udp_tutorials
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tests/tutorials/udp_processes.py
        --publisher $<TARGET_FILE:telemetry_publisher>
        --subscriber $<TARGET_FILE:telemetry_subscriber>
        --client $<TARGET_FILE:calculator_client>
        --server $<TARGET_FILE:calculator_server>)
    set_tests_properties(wirelink_udp_tutorials PROPERTIES TIMEOUT 30)
    add_test(NAME wirelink_udp_owned_strings
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tests/tutorials/udp_owned_strings.py
        --client $<TARGET_FILE:device_info_client>
        --server $<TARGET_FILE:device_info_server>)
    set_tests_properties(wirelink_udp_owned_strings PROPERTIES TIMEOUT 30)
    add_test(NAME wirelink_udp_async_deferred
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tests/tutorials/udp_rpc_variants.py
        --client $<TARGET_FILE:calculator_client>
        --async-client $<TARGET_FILE:calculator_client_async>
        --server $<TARGET_FILE:calculator_server>
        --deferred-server $<TARGET_FILE:calculator_server_deferred>)
    set_tests_properties(wirelink_udp_async_deferred PROPERTIES TIMEOUT 45)
  endif()
endif()

# The former combined tutorials remain deterministic regression tests only.
if(WIRELINK_BUILD_GETTING_STARTED AND BUILD_TESTING)
  wirelink_wlc_generate_codec(
    TARGET wirelink_temperature_codec
    SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/../tests/tutorials/loopback/temperature.wl")
  wirelink_wlc_generate_runtime(
    TARGET wirelink_temperature_protocol
    CODEC_TARGET wirelink_temperature_codec
    PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/../tests/tutorials/loopback/temperature.bind.wl")
  add_executable(wirelink_latest_telemetry ../tests/tutorials/loopback/latest_telemetry.c)
  target_link_libraries(wirelink_latest_telemetry PRIVATE
    wirelink_temperature_protocol Wirelink::loopback)
  wirelink_wlc_generate_codec(
    TARGET wirelink_quickstart_codec
    SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/../tests/tutorials/loopback/quickstart.wl")
  wirelink_wlc_generate_runtime(
    TARGET wirelink_quickstart_protocol
    CODEC_TARGET wirelink_quickstart_codec
    PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/../tests/tutorials/loopback/quickstart.bind.wl")
  add_executable(wirelink_getting_started ../tests/tutorials/loopback/getting_started.c)
  target_link_libraries(wirelink_getting_started PRIVATE
    wirelink_quickstart_protocol Wirelink::loopback)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_bare_metal_loopback PRIVATE
    -Wall -Wextra -Wpedantic)
  if(TARGET wirelink_getting_started)
    target_compile_options(wirelink_latest_telemetry PRIVATE
      -Wall -Wextra -Wpedantic)
    target_compile_options(wirelink_getting_started PRIVATE
      -Wall -Wextra -Wpedantic)
  endif()
endif()

if(BUILD_TESTING)
  add_test(NAME wirelink_bare_metal_loopback
    COMMAND wirelink_bare_metal_loopback)
  if(TARGET wirelink_getting_started)
    add_test(NAME wirelink_latest_telemetry
      COMMAND wirelink_latest_telemetry)
    add_test(NAME wirelink_getting_started
      COMMAND wirelink_getting_started)
  endif()
endif()

if(TARGET wirelink_astrial)
  add_executable(wirelink_astrial_typed_serial
    astrial_typed_serial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/wlc/generated/current/control.c)
  target_include_directories(wirelink_astrial_typed_serial PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/wlc/generated/current)
  target_link_libraries(wirelink_astrial_typed_serial PRIVATE wirelink::astrial)
  target_compile_features(wirelink_astrial_typed_serial PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wirelink_astrial_typed_serial PRIVATE
      -Wall -Wextra -Wpedantic)
  endif()
endif()
