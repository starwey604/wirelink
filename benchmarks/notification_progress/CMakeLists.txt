# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate(TARGET notification_protocol
  SCHEMA "${PROJECT_SOURCE_DIR}/tests/fixtures/static_kernel/usb.wl"
  PROFILE "${PROJECT_SOURCE_DIR}/tests/fixtures/static_kernel/usb.bind.wl")
add_executable(wirelink_notification_census census.c)
target_link_libraries(wirelink_notification_census PRIVATE notification_protocol)
wl_warnings(wirelink_notification_census PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BUILD_TESTING)
  add_test(NAME notification_census COMMAND wirelink_notification_census)
endif()
