cmake_minimum_required(VERSION 3.21)

project(wirelink_wlc_package_consumer LANGUAGES C)

include(CTest)
find_package(Wirelink 0.9 CONFIG REQUIRED)

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
