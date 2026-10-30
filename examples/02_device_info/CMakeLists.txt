# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate(TARGET device_info_protocol SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/device_info.wl"
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/device_info.bind.wl")
add_executable(wirelink_device_info_demo client.c server.c)
target_link_libraries(wirelink_device_info_demo PRIVATE device_info_protocol Wirelink::packet_loopback)
target_include_directories(wirelink_device_info_demo PRIVATE ../common)
wl_warnings(wirelink_device_info_demo PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BUILD_TESTING)
  add_test(NAME wirelink_device_info_demo COMMAND wirelink_device_info_demo)
endif()
