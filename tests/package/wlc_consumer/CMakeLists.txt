cmake_minimum_required(VERSION 3.21)

project(wirelink_wlc_package_consumer LANGUAGES C)

include(CTest)
find_package(Wirelink 0.9 CONFIG REQUIRED)

wirelink_wlc_generate(
  TARGET generated_control
  SCHEMA "${CMAKE_CURRENT_LIST_DIR}/../../fixtures/wlc/current/control.wl"
  PROFILE
    "${CMAKE_CURRENT_LIST_DIR}/../../fixtures/wlc/current/control.bind.wl")

add_executable(wirelink_wlc_package_consumer main.c)
target_link_libraries(wirelink_wlc_package_consumer PRIVATE generated_control)
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
