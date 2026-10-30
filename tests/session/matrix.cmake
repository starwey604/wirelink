# SPDX-License-Identifier: Apache-2.0
# The same generated kernel and identity cases run on host and Zephyr.
set(_session_dir "${CMAKE_CURRENT_LIST_DIR}")
wirelink_wlc_generate(TARGET session_protocol SCHEMA "${_session_dir}/session.wl"
  PROFILE "${_session_dir}/session.bind.wl")
target_sources(${SESSION_TEST_TARGET} PRIVATE "${_session_dir}/main.c" "${_session_dir}/cases.c")
target_link_libraries(${SESSION_TEST_TARGET} PRIVATE session_protocol)
