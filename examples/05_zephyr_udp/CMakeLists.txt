# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate_codec(TARGET udp_demo_codec SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/udp_demo.wl")
wirelink_wlc_generate_runtime(TARGET udp_demo_protocol CODEC_TARGET udp_demo_codec
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/udp_demo.bind.wl")
foreach(role client server)
  add_executable(udp_peer_${role} ${role}.c)
  target_link_libraries(udp_peer_${role} PRIVATE udp_demo_protocol wirelink_tutorial_host)
endforeach()
