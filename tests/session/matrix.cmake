# SPDX-License-Identifier: Apache-2.0
# Same generated code and cases run on desktop, simulators and the H7.
set(_session_dir "${CMAKE_CURRENT_LIST_DIR}")
wirelink_wlc_generate_codec(TARGET session_codec SCHEMA "${_session_dir}/session.wl")
if(TARGET zephyr_interface)
  target_link_libraries(session_codec PUBLIC zephyr_interface)
endif()
foreach(mode rr ru ur uu)
  string(TOUPPER "${mode}" upper_mode)
  wirelink_wlc_generate_runtime(TARGET session_${mode} CODEC_TARGET session_codec
    PROFILE "${_session_dir}/${mode}.bind.wl" RUNTIME_NAME ${mode})
  add_library(session_cases_${mode} STATIC "${_session_dir}/cases.c")
  target_compile_definitions(session_cases_${mode} PRIVATE
    SESSION_PREFIX=${mode} SESSION_UPPER=${upper_mode} SESSION_HEADER="${mode}_advanced.h")
  target_link_libraries(session_cases_${mode} PUBLIC session_${mode})
  target_link_libraries(${SESSION_TEST_TARGET} PRIVATE session_cases_${mode})
endforeach()
target_sources(${SESSION_TEST_TARGET} PRIVATE "${_session_dir}/main.c")
