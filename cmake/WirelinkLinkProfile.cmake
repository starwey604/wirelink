# SPDX-License-Identifier: Apache-2.0

# Also used by Zephyr and direct-source unit suites so the capability reaches
# every core translation unit, independently of application compile flags.
function(wirelink_target_link_profile target profile)
  if("${profile}" STREQUAL "multi")
    target_compile_definitions(${target} PRIVATE WL_LINK_PROFILE_COBS_CRC16=0)
  elseif("${profile}" STREQUAL "cobs-crc16")
    target_compile_definitions(${target} PRIVATE WL_LINK_PROFILE_COBS_CRC16=1)
  else()
    message(FATAL_ERROR "WIRELINK_LINK_PROFILE must be multi or cobs-crc16 (got '${profile}')")
  endif()
endfunction()
