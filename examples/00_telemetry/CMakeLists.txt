# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate_codec(TARGET telemetry_codec SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/telemetry.wl")
wirelink_wlc_generate_runtime(TARGET telemetry_protocol CODEC_TARGET telemetry_codec
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/telemetry.bind.wl")
foreach(role publisher subscriber)
  add_executable(telemetry_${role} ${role}.c)
  target_link_libraries(telemetry_${role} PRIVATE telemetry_protocol wirelink_tutorial_host)
endforeach()
