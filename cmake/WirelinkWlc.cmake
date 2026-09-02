include_guard(GLOBAL)

set(WIRELINK_WLC_VERSION "0.1.0")
set(WIRELINK_WLC_CODEGEN_ABI "8")
option(WIRELINK_WLC_AUTO_DOWNLOAD
  "Download the pinned WLC host compiler when it is not installed" ON)
set(WIRELINK_WLC_CACHE_DIR
  "${CMAKE_BINARY_DIR}/_deps/wirelink-wlc" CACHE PATH
  "Directory for verified WLC host compiler downloads")
mark_as_advanced(WIRELINK_WLC_CACHE_DIR)

function(_wirelink_wlc_validate_executable executable out_valid out_reason)
  if(NOT EXISTS "${executable}" OR IS_DIRECTORY "${executable}")
    set(${out_valid} FALSE PARENT_SCOPE)
    set(${out_reason} "does not name a file" PARENT_SCOPE)
    return()
  endif()

  execute_process(
    COMMAND "${executable}" --version
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    TIMEOUT 10)
  if(NOT _result STREQUAL "0")
    set(${out_valid} FALSE PARENT_SCOPE)
    set(${out_reason}
      "could not be executed (exit ${_result}): ${_stderr}" PARENT_SCOPE)
    return()
  endif()

  set(_expected "wlc ${WIRELINK_WLC_VERSION}")
  if(NOT _stdout STREQUAL _expected)
    set(${out_valid} FALSE PARENT_SCOPE)
    set(${out_reason}
      "reported '${_stdout}', expected '${_expected}'" PARENT_SCOPE)
    return()
  endif()

  set(${out_valid} TRUE PARENT_SCOPE)
  set(${out_reason} "" PARENT_SCOPE)
endfunction()

function(_wirelink_wlc_release_asset out_asset out_hash out_executable)
  string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _processor)
  if(_processor MATCHES "^(x86_64|amd64|x64)$")
    set(_architecture x86_64)
  elseif(_processor MATCHES "^(aarch64|arm64)$")
    set(_architecture aarch64)
  else()
    message(FATAL_ERROR
      "Wirelink has no pinned WLC ${WIRELINK_WLC_VERSION} release for host "
      "architecture '${CMAKE_HOST_SYSTEM_PROCESSOR}'. Set WLC_EXECUTABLE "
      "for this call or WIRELINK_WLC_EXECUTABLE for the project.")
  endif()

  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND
      _architecture STREQUAL "x86_64")
    set(_asset "wlc-windows-x86_64.zip")
    set(_hash "244b1278a1522fa898c67b57e249fd611f4c9b0f675fd74db1030fad8dc35099")
    set(_executable "wlc.exe")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
      _architecture STREQUAL "x86_64")
    set(_asset "wlc-linux-x86_64-musl.tar.gz")
    set(_hash "c8cdf348cd2cdb984b9cea03301d3a1716399e4fbf4f8923bdee850b3c710cc8")
    set(_executable "wlc")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
      _architecture STREQUAL "aarch64")
    set(_asset "wlc-linux-aarch64-musl.tar.gz")
    set(_hash "63e7d7f9b0e83d020213fe296189ab725b491ba6417e02ce14acd51adb9466c5")
    set(_executable "wlc")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
      _architecture STREQUAL "x86_64")
    set(_asset "wlc-macos-x86_64.tar.gz")
    set(_hash "e94f46c6e946b8de0c916517941a3fb9418d16bcc523ceaf2b8df58cc2603a82")
    set(_executable "wlc")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
      _architecture STREQUAL "aarch64")
    set(_asset "wlc-macos-aarch64.tar.gz")
    set(_hash "ba0992f7534da2607da7af4cbe820a419c0045f3617883c8442a5d0f1db113d4")
    set(_executable "wlc")
  else()
    message(FATAL_ERROR
      "Wirelink has no pinned WLC ${WIRELINK_WLC_VERSION} release for host "
      "'${CMAKE_HOST_SYSTEM_NAME}/${CMAKE_HOST_SYSTEM_PROCESSOR}'. Set "
      "WLC_EXECUTABLE for this call or WIRELINK_WLC_EXECUTABLE for the project.")
  endif()

  set(${out_asset} "${_asset}" PARENT_SCOPE)
  set(${out_hash} "${_hash}" PARENT_SCOPE)
  set(${out_executable} "${_executable}" PARENT_SCOPE)
endfunction()

function(_wirelink_wlc_download out_executable)
  _wirelink_wlc_release_asset(_asset _expected_hash _executable_name)
  get_filename_component(_cache_dir "${WIRELINK_WLC_CACHE_DIR}" ABSOLUTE
    BASE_DIR "${CMAKE_BINARY_DIR}")
  set(_version_dir "${_cache_dir}/v${WIRELINK_WLC_VERSION}")
  set(_extract_dir "${_version_dir}/${_asset}.contents")
  set(_executable "${_extract_dir}/${_executable_name}")

  _wirelink_wlc_validate_executable("${_executable}" _cached_valid
    _cached_reason)
  if(_cached_valid)
    set(${out_executable} "${_executable}" PARENT_SCOPE)
    return()
  endif()

  file(MAKE_DIRECTORY "${_version_dir}")
  file(LOCK "${_version_dir}/download.lock" GUARD FUNCTION TIMEOUT 180
    RESULT_VARIABLE _lock_result)
  if(NOT _lock_result STREQUAL "0")
    message(FATAL_ERROR
      "Could not lock the WLC download cache '${_version_dir}': ${_lock_result}")
  endif()

  # Another configure process may have populated the cache while this one
  # waited for the lock.
  _wirelink_wlc_validate_executable("${_executable}" _cached_valid
    _cached_reason)
  if(_cached_valid)
    set(${out_executable} "${_executable}" PARENT_SCOPE)
    return()
  endif()

  set(_archive "${_version_dir}/${_asset}")
  if(EXISTS "${_archive}")
    file(SHA256 "${_archive}" _archive_hash)
    if(NOT _archive_hash STREQUAL _expected_hash)
      file(REMOVE "${_archive}")
    endif()
  endif()

  if(NOT EXISTS "${_archive}")
    set(_url
      "https://github.com/starwey604/wlc/releases/download/v${WIRELINK_WLC_VERSION}/${_asset}")
    set(_partial "${_archive}.part")
    file(REMOVE "${_partial}")
    message(STATUS
      "Wirelink: downloading WLC ${WIRELINK_WLC_VERSION} for the build host")
    file(DOWNLOAD "${_url}" "${_partial}"
      EXPECTED_HASH "SHA256=${_expected_hash}"
      TLS_VERIFY ON
      STATUS _download_status
      SHOW_PROGRESS)
    list(GET _download_status 0 _download_code)
    list(GET _download_status 1 _download_message)
    if(NOT _download_code STREQUAL "0")
      file(REMOVE "${_partial}")
      message(FATAL_ERROR
        "Could not download pinned WLC asset '${_url}': ${_download_message}")
    endif()
    file(RENAME "${_partial}" "${_archive}")
  endif()

  set(_temporary_extract "${_extract_dir}.tmp")
  file(REMOVE_RECURSE "${_temporary_extract}")
  file(MAKE_DIRECTORY "${_temporary_extract}")
  file(ARCHIVE_EXTRACT INPUT "${_archive}"
    DESTINATION "${_temporary_extract}")
  set(_temporary_executable
    "${_temporary_extract}/${_executable_name}")
  if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND
      EXISTS "${_temporary_executable}")
    file(CHMOD "${_temporary_executable}"
      PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
  endif()
  _wirelink_wlc_validate_executable("${_temporary_executable}"
    _download_valid _download_reason)
  if(NOT _download_valid)
    file(REMOVE_RECURSE "${_temporary_extract}")
    message(FATAL_ERROR
      "Downloaded WLC asset '${_asset}' ${_download_reason}")
  endif()

  file(REMOVE_RECURSE "${_extract_dir}")
  file(RENAME "${_temporary_extract}" "${_extract_dir}")
  set(${out_executable} "${_executable}" PARENT_SCOPE)
endfunction()

function(_wirelink_wlc_resolve explicit_executable out_executable)
  if(explicit_executable)
    get_filename_component(_wlc "${explicit_executable}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    _wirelink_wlc_validate_executable("${_wlc}" _valid _reason)
    if(NOT _valid)
      message(FATAL_ERROR "WLC_EXECUTABLE '${_wlc}' ${_reason}")
    endif()
    set(${out_executable} "${_wlc}" PARENT_SCOPE)
    return()
  endif()

  if(WIRELINK_WLC_EXECUTABLE)
    get_filename_component(_wlc "${WIRELINK_WLC_EXECUTABLE}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    _wirelink_wlc_validate_executable("${_wlc}" _valid _reason)
    if(NOT _valid)
      message(FATAL_ERROR "WIRELINK_WLC_EXECUTABLE '${_wlc}' ${_reason}")
    endif()
    set(${out_executable} "${_wlc}" PARENT_SCOPE)
    return()
  endif()

  find_program(_wlc_on_path NAMES wlc NO_CACHE NO_CMAKE_FIND_ROOT_PATH)
  if(_wlc_on_path)
    _wirelink_wlc_validate_executable("${_wlc_on_path}" _valid _reason)
    if(_valid)
      set(${out_executable} "${_wlc_on_path}" PARENT_SCOPE)
      return()
    endif()
    message(STATUS
      "Wirelink: ignoring host PATH compiler '${_wlc_on_path}': ${_reason}")
  endif()

  if(NOT WIRELINK_WLC_AUTO_DOWNLOAD)
    message(FATAL_ERROR
      "WLC ${WIRELINK_WLC_VERSION} was not found on the host PATH and "
      "WIRELINK_WLC_AUTO_DOWNLOAD is OFF. Set WLC_EXECUTABLE for this call "
      "or WIRELINK_WLC_EXECUTABLE for the project.")
  endif()

  _wirelink_wlc_download(_wlc)
  set(${out_executable} "${_wlc}" PARENT_SCOPE)
endfunction()

# Generate and compile the C artifacts for one Wirelink schema.  WLC is a
# host tool even when the consuming target is cross compiled, so automatic
# discovery deliberately ignores CMAKE_FIND_ROOT_PATH.
function(wirelink_wlc_generate)
  set(_options)
  set(_one_value_args
    TARGET
    SCHEMA
    PROFILE
    PREVIOUS
    OUTPUT_DIR
    WLC_EXECUTABLE)
  cmake_parse_arguments(WLC
    "${_options}" "${_one_value_args}" "" ${ARGN})

  if(WLC_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "wirelink_wlc_generate received unknown arguments: "
      "${WLC_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT WLC_TARGET)
    message(FATAL_ERROR "wirelink_wlc_generate requires TARGET")
  endif()
  if(TARGET "${WLC_TARGET}")
    message(FATAL_ERROR
      "wirelink_wlc_generate target '${WLC_TARGET}' already exists")
  endif()
  if(NOT WLC_SCHEMA)
    message(FATAL_ERROR "wirelink_wlc_generate requires SCHEMA")
  endif()

  get_filename_component(_schema "${WLC_SCHEMA}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT EXISTS "${_schema}")
    message(FATAL_ERROR "Wirelink schema does not exist: ${_schema}")
  endif()
  get_filename_component(_module "${_schema}" NAME_WE)

  if(WLC_PROFILE)
    get_filename_component(_profile "${WLC_PROFILE}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${_profile}")
      message(FATAL_ERROR "Wirelink binding profile does not exist: ${_profile}")
    endif()
  endif()
  if(WLC_PREVIOUS)
    get_filename_component(_previous "${WLC_PREVIOUS}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${_previous}")
      message(FATAL_ERROR "Previous Wirelink schema does not exist: ${_previous}")
    endif()
  endif()

  if(WLC_OUTPUT_DIR)
    get_filename_component(_output_dir "${WLC_OUTPUT_DIR}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  else()
    set(_output_dir
      "${CMAKE_CURRENT_BINARY_DIR}/wirelink-generated/${WLC_TARGET}")
  endif()

  _wirelink_wlc_resolve("${WLC_WLC_EXECUTABLE}" _wlc)

  set(_generated
    "${_output_dir}/${_module}.h"
    "${_output_dir}/${_module}.c"
    "${_output_dir}/${_module}_bindings.h"
    "${_output_dir}/${_module}_bindings.c"
    "${_output_dir}/${_module}_manifest.json")
  set(_generated_sources
    "${_output_dir}/${_module}.c"
    "${_output_dir}/${_module}_bindings.c")
  set(_manifest "${_output_dir}/${_module}_manifest.json")
  if(WLC_PROFILE)
    list(APPEND _generated
      "${_output_dir}/${_module}_runtime.h"
      "${_output_dir}/${_module}_runtime.c")
    list(APPEND _generated_sources
      "${_output_dir}/${_module}_runtime.c")
  endif()

  set(_command
    "${_wlc}" compile "${_schema}" --out-dir "${_output_dir}")
  set(_depends "${_schema}" "${_wlc}")
  if(WLC_PREVIOUS)
    list(APPEND _command --previous "${_previous}")
    list(APPEND _depends "${_previous}")
  endif()
  if(WLC_PROFILE)
    list(APPEND _command --profile "${_profile}")
    list(APPEND _depends "${_profile}")
  endif()

  set(_codegen_stamp "${_output_dir}/.${_module}-wlc-codegen.stamp")
  set(_manifest_verifier
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WirelinkWlcVerifyManifest.cmake")
  add_custom_command(
    OUTPUT "${_codegen_stamp}"
    BYPRODUCTS ${_generated}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output_dir}"
    COMMAND ${_command}
    COMMAND "${CMAKE_COMMAND}"
      "-DWIRELINK_WLC_MANIFEST=${_manifest}"
      "-DWIRELINK_WLC_EXPECTED_VERSION=${WIRELINK_WLC_VERSION}"
      "-DWIRELINK_WLC_EXPECTED_ABI=${WIRELINK_WLC_CODEGEN_ABI}"
      -P "${_manifest_verifier}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_codegen_stamp}"
    DEPENDS ${_depends} "${_manifest_verifier}"
    COMMENT "Generating Wirelink C bindings for ${_module}.wl"
    COMMAND_EXPAND_LISTS
    VERBATIM)

  set_source_files_properties(${_generated_sources} PROPERTIES GENERATED TRUE)
  add_custom_target("${WLC_TARGET}_wlc_codegen" DEPENDS "${_codegen_stamp}")
  add_library("${WLC_TARGET}" STATIC ${_generated_sources})
  add_dependencies("${WLC_TARGET}" "${WLC_TARGET}_wlc_codegen")
  target_include_directories("${WLC_TARGET}" PUBLIC
    "$<BUILD_INTERFACE:${_output_dir}>")
  target_compile_features("${WLC_TARGET}" PUBLIC c_std_11)
  if(TARGET Wirelink::wirelink)
    target_link_libraries("${WLC_TARGET}" PUBLIC Wirelink::wirelink)
  elseif(TARGET wirelink)
    target_link_libraries("${WLC_TARGET}" PUBLIC wirelink)
  else()
    message(FATAL_ERROR
      "wirelink_wlc_generate requires the Wirelink::wirelink target")
  endif()

  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_GENERATED_DIR "${_output_dir}")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_SCHEMA "${_schema}")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_EXECUTABLE "${_wlc}")
  if(WLC_PROFILE)
    set_property(TARGET "${WLC_TARGET}" PROPERTY
      WIRELINK_WLC_PROFILE "${_profile}")
  endif()
endfunction()
