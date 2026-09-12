# SPDX-License-Identifier: ISC
include_guard(GLOBAL)

# Published v0.8.0 source pair; digest verified against the remote archive.
set(WIRELINK_WLC_SOURCE_ABI "32" CACHE INTERNAL
  "Codegen ABI of the last distributed WLC source pair" FORCE)
set(WIRELINK_WLC_SOURCE_REVISION "758d12a2466e254cb4d913fce578fb7deae766ec"
  CACHE INTERNAL "Paired WLC source commit" FORCE)
set(WIRELINK_WLC_SOURCE_SHA256
  "c97fa894b7996c5d936862426d18394fe88cf2a6e06de455e65fbf8ea43df4e1"
  CACHE INTERNAL "Paired WLC source archive digest" FORCE)

# Select the host, never CMAKE_SYSTEM_PROCESSOR (which names the firmware target).
function(_wirelink_wlc_release_asset system processor out_asset out_digest)
  string(TOLOWER "${processor}" _processor)
  set(_asset "")
  set(_digest "")
  if(system STREQUAL "Windows" AND _processor MATCHES "^(amd64|x86_64)$")
    set(_asset "wlc-windows-x86_64.zip")
    set(_digest "203ac910e0cacaa8732aab69a0b9cae27e6c32003007420ec1838724f41ef4c6")
  elseif(system STREQUAL "Linux" AND _processor MATCHES "^(amd64|x86_64)$")
    set(_asset "wlc-linux-x86_64-musl.tar.gz")
    set(_digest "f5e88c221925def1a85c9940057ba55e46c14d869775275d2812bd1af6081c67")
  elseif(system STREQUAL "Linux" AND _processor MATCHES "^(arm64|aarch64)$")
    set(_asset "wlc-linux-aarch64-musl.tar.gz")
    set(_digest "79851f5b6a7fe59c5bb8f12287f96d47b0f6dd4b5cb7bc2daa0be69ea89c2445")
  elseif(system STREQUAL "Darwin" AND _processor MATCHES "^(amd64|x86_64)$")
    set(_asset "wlc-macos-x86_64.tar.gz")
    set(_digest "ae054c0978c5a79f91cb0dc5ce626ff97b9967ceeba071cf69fa95d1666b148e")
  elseif(system STREQUAL "Darwin" AND _processor MATCHES "^(arm64|aarch64)$")
    set(_asset "wlc-macos-aarch64.tar.gz")
    set(_digest "99dfd609a577f23b341baa2dda99c942f44b096c55051ee1fa2c7425d59f02b8")
  endif()
  set(${out_asset} "${_asset}" PARENT_SCOPE)
  set(${out_digest} "${_digest}" PARENT_SCOPE)
endfunction()

function(_wirelink_wlc_download_binary asset digest out_executable)
  get_filename_component(_cache "${WIRELINK_WLC_CACHE_DIR}" ABSOLUTE
    BASE_DIR "${CMAKE_BINARY_DIR}")
  set(_root "${_cache}/v${WIRELINK_WLC_VERSION}/${asset}")
  set(_binary "${_root}/bin/wlc")
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
    message(FATAL_ERROR "Cannot lock WLC download cache '${_root}': ${_lock}")
  endif()
  _wirelink_wlc_validate_executable("${_binary}" _valid _reason)
  if(_valid)
    set(${out_executable} "${_binary}" PARENT_SCOPE)
    return()
  endif()
  set(_archive "${_root}/${asset}")
  if(EXISTS "${_archive}")
    file(SHA256 "${_archive}" _actual_digest)
    if(NOT _actual_digest STREQUAL digest)
      message(FATAL_ERROR "WLC host archive cache digest mismatch: ${_archive}")
    endif()
  else()
    message(STATUS "Wirelink: fetching WLC ${WIRELINK_WLC_VERSION} host tool ${asset}")
    file(DOWNLOAD
      "https://github.com/starwey604/wlc/releases/download/v${WIRELINK_WLC_VERSION}/${asset}"
      "${_archive}.part" TLS_VERIFY ON TIMEOUT 120 INACTIVITY_TIMEOUT 30
      STATUS _download)
    list(GET _download 0 _code)
    if(NOT _code EQUAL 0)
      message(FATAL_ERROR "Cannot fetch paired WLC host tool: ${_download}")
    endif()
    file(SHA256 "${_archive}.part" _actual_digest)
    if(NOT _actual_digest STREQUAL digest)
      message(FATAL_ERROR "Downloaded WLC host archive digest mismatch: ${_archive}.part")
    endif()
    file(RENAME "${_archive}.part" "${_archive}")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_root}/bin")
  _wirelink_wlc_validate_executable("${_binary}" _valid _reason)
  if(NOT _valid)
    message(FATAL_ERROR "Downloaded WLC ${_reason}: ${_binary}")
  endif()
  set(${out_executable} "${_binary}" PARENT_SCOPE)
endfunction()

function(_wirelink_wlc_bootstrap out_executable)
  if(NOT WIRELINK_WLC_SOURCE_ABI STREQUAL WIRELINK_WLC_CODEGEN_ABI)
    message(FATAL_ERROR
      "WLC development ABI ${WIRELINK_WLC_CODEGEN_ABI} has no published source pair yet. "
      "Build the matching development compiler and set WIRELINK_WLC_EXECUTABLE. "
      "The pinned ABI ${WIRELINK_WLC_SOURCE_ABI} compiler cannot generate this API.")
  endif()
  cmake_host_system_information(RESULT _processor QUERY OS_PLATFORM)
  _wirelink_wlc_release_asset("${CMAKE_HOST_SYSTEM_NAME}" "${_processor}"
    _asset _digest)
  if(_asset)
    _wirelink_wlc_download_binary("${_asset}" "${_digest}" _binary)
    set(${out_executable} "${_binary}" PARENT_SCOPE)
    return()
  endif()
  find_program(_cargo NAMES cargo NO_CMAKE_FIND_ROOT_PATH)
  find_program(_rustc NAMES rustc NO_CMAKE_FIND_ROOT_PATH)
  if(NOT _cargo OR NOT _rustc)
    message(FATAL_ERROR
      "WLC ABI ${WIRELINK_WLC_CODEGEN_ABI} needs a matching host binary or "
      "Rust/Cargo (Rust 2024 edition) to build the pinned source. "
      "Set WIRELINK_WLC_EXECUTABLE or install Rust/Cargo on the host PATH. "
      "There is no prebuilt WLC package for ${CMAKE_HOST_SYSTEM_NAME}/${_processor}.")
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
