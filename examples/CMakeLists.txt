# SPDX-License-Identifier: Apache-2.0
# Checked-in deterministic WLC output keeps the default C build tool-independent.
add_library(wirelink_example_codec STATIC kernel/generated/demo.c kernel/generated/demo_kernel.c)
target_include_directories(wirelink_example_codec PUBLIC kernel/generated)
target_link_libraries(wirelink_example_codec PUBLIC wirelink_kernel)
wl_warnings(wirelink_example_codec PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(wirelink_bare_metal_loopback bare_metal_loopback.c)
target_link_libraries(wirelink_bare_metal_loopback PRIVATE wirelink_example_codec wirelink_packet_loopback)
wl_warnings(wirelink_bare_metal_loopback PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BUILD_TESTING)
  add_test(NAME wirelink_bare_metal_loopback COMMAND wirelink_bare_metal_loopback)
endif()

if(WIRELINK_BUILD_WLC_EXAMPLES)
  add_subdirectory(00_telemetry)
  add_subdirectory(01_rpc)
  add_subdirectory(02_device_info)
  add_subdirectory(03_device_service)
  add_subdirectory(04_composed_services)
endif()

if(WIRELINK_BUILD_WLC_EXAMPLES AND WIRELINK_BUILD_ASIO_UDP_ADAPTER AND WIRELINK_BUILD_PLATFORM)
  add_subdirectory(05_zephyr_udp)
endif()
