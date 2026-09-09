cmake_minimum_required(VERSION 3.21)

project(wirelink_wlc_package_consumer LANGUAGES C)

include(CTest)
find_package(Wirelink 0.6 CONFIG REQUIRED)

wirelink_wlc_generate_codec(
  TARGET generated_control_codec
  SCHEMA "${CMAKE_CURRENT_LIST_DIR}/../../fixtures/wlc/current/control.wl")
wirelink_wlc_generate_runtime(
  TARGET generated_control
  CODEC_TARGET generated_control_codec
  PROFILE
    "${CMAKE_CURRENT_LIST_DIR}/../../fixtures/wlc/current/control.bind.wl")
wirelink_wlc_generate_runtime(
  TARGET generated_control_sender
  CODEC_TARGET generated_control_codec
  PROFILE
    "${CMAKE_CURRENT_LIST_DIR}/../../fixtures/wlc/current/control_sender.bind.wl"
  RUNTIME_NAME control_sender)

# Keep the one-runtime composition wrapper covered while new code uses the
# explicit codec/runtime boundary above.
wirelink_wlc_generate(
  TARGET generated_control_legacy
  SCHEMA "${CMAKE_CURRENT_LIST_DIR}/../../fixtures/wlc/current/control.wl"
  PROFILE
    "${CMAKE_CURRENT_LIST_DIR}/../../fixtures/wlc/current/control.bind.wl")

add_executable(wirelink_wlc_package_consumer main.c)
target_link_libraries(wirelink_wlc_package_consumer PRIVATE
  generated_control generated_control_sender)
set_target_properties(wirelink_wlc_package_consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO)

if(MSVC)
  target_compile_options(wirelink_wlc_package_consumer PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_wlc_package_consumer PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

if(TARGET Wirelink::platform)
  add_executable(wirelink_wlc_platform_consumer platform.c)
  target_link_libraries(wirelink_wlc_platform_consumer PRIVATE
    generated_calculator Wirelink::platform)
  set_target_properties(wirelink_wlc_platform_consumer PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
  add_test(NAME wirelink_wlc_platform_consumer COMMAND wirelink_wlc_platform_consumer)
endif()

add_test(NAME wirelink_wlc_package_consumer
  COMMAND wirelink_wlc_package_consumer)

add_executable(wirelink_wlc_legacy_consumer legacy.c)
target_link_libraries(wirelink_wlc_legacy_consumer PRIVATE
  generated_control_legacy)
set_target_properties(wirelink_wlc_legacy_consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO)
add_test(NAME wirelink_wlc_legacy_consumer
  COMMAND wirelink_wlc_legacy_consumer)

# Ordinary managed RPC must also build against installed headers, not just the
# historical mapped profile above (which does not include rpc_async/rpc_sync).
wirelink_wlc_generate(
  TARGET generated_calculator
  SCHEMA "${CMAKE_CURRENT_LIST_DIR}/../../../examples/01_rpc/calculator.wl"
  PROFILE "${CMAKE_CURRENT_LIST_DIR}/../../../examples/01_rpc/calculator.bind.wl")
add_executable(wirelink_wlc_rpc_consumer rpc.c)
target_link_libraries(wirelink_wlc_rpc_consumer PRIVATE generated_calculator)
set_target_properties(wirelink_wlc_rpc_consumer PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
add_test(NAME wirelink_wlc_rpc_consumer COMMAND wirelink_wlc_rpc_consumer)

# Composed profiles must work through the installed CMake helper, too.
set(_device_example "${CMAKE_CURRENT_LIST_DIR}/../../../examples/03_device_service")
wirelink_wlc_generate_codec(TARGET generated_device_codec
  SCHEMA "${_device_example}/device.wl")
wirelink_wlc_generate_runtime(TARGET generated_device_server
  CODEC_TARGET generated_device_codec RUNTIME_NAME device_server
  PROFILES "${_device_example}/services.bind.wl" "${_device_example}/server.bind.wl")
add_executable(wirelink_wlc_composed_consumer composed.c)
target_link_libraries(wirelink_wlc_composed_consumer PRIVATE generated_device_server)
set_target_properties(wirelink_wlc_composed_consumer PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(MSVC)
  target_compile_options(wirelink_wlc_composed_consumer PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_wlc_composed_consumer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME wirelink_wlc_composed_consumer COMMAND wirelink_wlc_composed_consumer)

if(TARGET Wirelink::storage)
  add_executable(wirelink_wlc_storage_consumer storage.c)
  target_link_libraries(wirelink_wlc_storage_consumer PRIVATE generated_calculator Wirelink::storage)
  set_target_properties(wirelink_wlc_storage_consumer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
  add_test(NAME wirelink_wlc_storage_consumer COMMAND wirelink_wlc_storage_consumer)
endif()
