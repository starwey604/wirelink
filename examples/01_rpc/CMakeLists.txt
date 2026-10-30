# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate(TARGET calculator_protocol SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/calculator.wl"
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/calculator.bind.wl")
add_executable(wirelink_calculator_demo client.c client_async.c server.c)
target_link_libraries(wirelink_calculator_demo PRIVATE calculator_protocol Wirelink::packet_loopback)
target_include_directories(wirelink_calculator_demo PRIVATE ../common)
wl_warnings(wirelink_calculator_demo PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BUILD_TESTING)
  add_test(NAME wirelink_calculator_demo COMMAND wirelink_calculator_demo)
endif()
