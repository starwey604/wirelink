# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate_codec(TARGET device_info_codec
  SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/device_info.wl")
wirelink_wlc_generate_runtime(TARGET device_info_protocol
  CODEC_TARGET device_info_codec PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/device_info.bind.wl")
foreach(role client server)
  add_executable(device_info_${role} ${role}.c)
  target_link_libraries(device_info_${role} PRIVATE device_info_protocol wirelink_tutorial_host)
endforeach()
