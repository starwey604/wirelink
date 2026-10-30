# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate_codec(TARGET usb_sample_control
  SCHEMA "${PROJECT_SOURCE_DIR}/tests/fixtures/wlc/current/control.wl")
add_executable(wirelink_usb_application main.c
  ${PROJECT_SOURCE_DIR}/samples/zephyr/usb_bulk/src/application.c)
target_include_directories(wirelink_usb_application PRIVATE
  ${PROJECT_SOURCE_DIR}/samples/zephyr/usb_bulk/src ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wirelink_usb_application PRIVATE usb_sample_control Wirelink::kernel Wirelink::framing_primitives)
wl_warnings(wirelink_usb_application PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME wirelink_usb_application COMMAND wirelink_usb_application)
