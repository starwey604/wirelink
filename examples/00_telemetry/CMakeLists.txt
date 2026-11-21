# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate(TARGET telemetry_pub_protocol SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/telemetry.wl" NODE pub)
wirelink_wlc_generate(TARGET telemetry_sub_protocol SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/telemetry.wl" NODE sub)
add_executable(telemetry_publisher publisher.c)
add_executable(telemetry_subscriber subscriber.c)
target_link_libraries(telemetry_publisher PRIVATE telemetry_pub_protocol wirelink_tutorial_host)
target_link_libraries(telemetry_subscriber PRIVATE telemetry_sub_protocol wirelink_tutorial_host)
wl_warnings(telemetry_publisher PRIVATE -Wall -Wextra -Wpedantic -Werror)
wl_warnings(telemetry_subscriber PRIVATE -Wall -Wextra -Wpedantic -Werror)
