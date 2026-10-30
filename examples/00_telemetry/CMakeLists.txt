# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate(TARGET telemetry_protocol SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/telemetry.wl"
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/telemetry.bind.wl")
add_executable(wirelink_telemetry_demo publisher.c subscriber.c)
target_link_libraries(wirelink_telemetry_demo PRIVATE telemetry_protocol Wirelink::packet_loopback)
target_include_directories(wirelink_telemetry_demo PRIVATE ../common)
wl_warnings(wirelink_telemetry_demo PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BUILD_TESTING)
  add_test(NAME wirelink_telemetry_demo COMMAND wirelink_telemetry_demo)
endif()
