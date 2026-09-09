# SPDX-License-Identifier: ISC
include_guard(GLOBAL)

# Published v0.5.0 source pair; digest verified against the remote archive.
set(WIRELINK_WLC_SOURCE_ABI "30" CACHE INTERNAL
  "Codegen ABI of the last distributed WLC source pair" FORCE)
set(WIRELINK_WLC_SOURCE_REVISION "120b9af130753d2ba0d137882916bfe207d3d312"
  CACHE INTERNAL "Paired WLC source commit" FORCE)
set(WIRELINK_WLC_SOURCE_SHA256
  "db2d6d62d01a612e7af11eeb80b072c39cbee90146b2de186ddaac94c5b99962"
  CACHE INTERNAL "Paired WLC source archive digest" FORCE)

function(_wirelink_wlc_bootstrap out_executable)
  if(NOT WIRELINK_WLC_SOURCE_ABI STREQUAL WIRELINK_WLC_CODEGEN_ABI)
    message(FATAL_ERROR
      "WLC development ABI ${WIRELINK_WLC_CODEGEN_ABI} has no published source pair yet. "
      "Build the matching development compiler and set WIRELINK_WLC_EXECUTABLE. "
      "The pinned ABI ${WIRELINK_WLC_SOURCE_ABI} compiler cannot generate this API.")
  endif()
  find_program(_cargo NAMES cargo NO_CMAKE_FIND_ROOT_PATH)
  find_program(_rustc NAMES rustc NO_CMAKE_FIND_ROOT_PATH)
  if(NOT _cargo OR NOT _rustc)
    message(FATAL_ERROR
      "WLC ABI ${WIRELINK_WLC_CODEGEN_ABI} needs a matching host binary or "
      "Rust/Cargo (Rust 2024 edition) to build the pinned source. "
      "Set WIRELINK_WLC_EXECUTABLE or install Rust/Cargo on the host PATH. "
      "No compatible public release binary is assumed.")
  endif()
  execute_process(COMMAND "${_rustc}" -vV RESULT_VARIABLE _result
    OUTPUT_VARIABLE _version ERROR_VARIABLE _error TIMEOUT 30)
  if(NOT _result STREQUAL "0" OR
      NOT _version MATCHES "host: ([A-Za-z0-9_-]+)")
    message(FATAL_ERROR "Cannot identify the WLC build host: ${_error}")
  endif()
  set(_host "${CMAKE_MATCH_1}")
  get_filename_component(_cache "${WIRELINK_WLC_CACHE_DIR}" ABSOLUTE
    BASE_DIR "${CMAKE_BINARY_DIR}")
  set(_root "${_cache}/${WIRELINK_WLC_SOURCE_REVISION}/${_host}")
  set(_binary "${_root}/target/${_host}/release/wlc")
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    string(APPEND _binary ".exe")
  endif()
  _wirelink_wlc_validate_executable("${_binary}" _valid _reason)
  if(_valid)
    set(${out_executable} "${_binary}" PARENT_SCOPE)
    return()
  endif()
  file(MAKE_DIRECTORY "${_root}")
  file(LOCK "${_root}/bootstrap.lock" GUARD FUNCTION TIMEOUT 600
    RESULT_VARIABLE _lock)
  if(NOT _lock STREQUAL "0")
    message(FATAL_ERROR "Cannot lock WLC bootstrap cache '${_root}': ${_lock}")
  endif()
  _wirelink_wlc_validate_executable("${_binary}" _valid _reason)
  if(_valid)
    set(${out_executable} "${_binary}" PARENT_SCOPE)
    return()
  endif()

  set(_archive "${_root}/source.tar.gz")
  if(EXISTS "${_archive}")
    file(SHA256 "${_archive}" _digest)
    if(NOT _digest STREQUAL WIRELINK_WLC_SOURCE_SHA256)
      message(FATAL_ERROR "WLC source cache digest mismatch: ${_archive}")
    endif()
  else()
    message(STATUS "Wirelink: fetching WLC source ${WIRELINK_WLC_SOURCE_REVISION}")
    file(DOWNLOAD
      "https://codeload.github.com/starwey604/wlc/tar.gz/${WIRELINK_WLC_SOURCE_REVISION}"
      "${_archive}.part" TLS_VERIFY ON TIMEOUT 120 INACTIVITY_TIMEOUT 30
      STATUS _download)
    list(GET _download 0 _code)
    if(NOT _code EQUAL 0)
      message(FATAL_ERROR "Cannot fetch paired WLC source: ${_download}")
    endif()
    file(SHA256 "${_archive}.part" _digest)
    if(NOT _digest STREQUAL WIRELINK_WLC_SOURCE_SHA256)
      message(FATAL_ERROR "Downloaded WLC source digest mismatch: ${_archive}.part")
    endif()
    file(RENAME "${_archive}.part" "${_archive}")
  endif()
  # Re-extract the verified archive after an interrupted/failed build. The
  # cache is private to this source commit and host, not an editable worktree.
  file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_root}/source")
  set(_source "${_root}/source/wlc-${WIRELINK_WLC_SOURCE_REVISION}")
  message(STATUS "Wirelink: building WLC ABI ${WIRELINK_WLC_CODEGEN_ABI} for ${_host}")
  execute_process(
    COMMAND "${_cargo}" build --release --locked --jobs 2
      --manifest-path "${_source}/Cargo.toml" --target "${_host}"
      --target-dir "${_root}/target"
    WORKING_DIRECTORY "${_source}"
    RESULT_VARIABLE _result OUTPUT_FILE "${_root}/build.log"
    ERROR_FILE "${_root}/build.log" TIMEOUT 600)
  if(NOT _result STREQUAL "0")
    message(FATAL_ERROR
      "Pinned WLC host build failed (${_result}); see ${_root}/build.log. "
      "Use a current Rust/Cargo, or set WIRELINK_WLC_EXECUTABLE.")
  endif()
  _wirelink_wlc_validate_executable("${_binary}" _valid _reason)
  if(NOT _valid)
    message(FATAL_ERROR "Built WLC ${_reason}; see ${_root}/build.log")
  endif()
  set(${out_executable} "${_binary}" PARENT_SCOPE)
endfunction()
