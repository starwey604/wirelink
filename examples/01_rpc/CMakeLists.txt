# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate_codec(TARGET calculator_codec SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/calculator.wl")
wirelink_wlc_generate_runtime(TARGET calculator_protocol CODEC_TARGET calculator_codec
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/calculator.bind.wl")
foreach(role client server)
  add_executable(calculator_${role} ${role}.c)
  target_link_libraries(calculator_${role} PRIVATE calculator_protocol wirelink_tutorial_host)
endforeach()
