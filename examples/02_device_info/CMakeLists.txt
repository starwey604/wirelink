# SPDX-License-Identifier: Apache-2.0
foreach(role client server)
  wirelink_wlc_generate(TARGET device_info_${role}_protocol
    SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/device_info.wl" NODE info_${role})
  add_executable(device_info_${role} ${role}.c)
  target_link_libraries(device_info_${role} PRIVATE device_info_${role}_protocol wirelink_tutorial_host)
  wl_warnings(device_info_${role} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endforeach()
