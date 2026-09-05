include_guard(GLOBAL)

# These values are consumed by functions that may be called from a parent
# directory after Wirelink itself was added with add_subdirectory(). Keep them
# in the global CMake cache so function call-site scope cannot hide them.
set(WIRELINK_WLC_VERSION "0.4.0" CACHE INTERNAL
  "Pinned WLC host compiler version" FORCE)
set(WIRELINK_WLC_CODEGEN_ABI "20" CACHE INTERNAL
  "Pinned WLC generated-code ABI" FORCE)
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

  execute_process(
    COMMAND "${executable}" codegen-abi
    RESULT_VARIABLE _abi_result
    OUTPUT_VARIABLE _abi
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
    TIMEOUT 10)
  if(NOT _abi_result STREQUAL "0" OR NOT _abi STREQUAL "${WIRELINK_WLC_CODEGEN_ABI}")
    set(${out_valid} FALSE PARENT_SCOPE)
    set(${out_reason}
      "does not provide codegen ABI ${WIRELINK_WLC_CODEGEN_ABI}; install the matching WLC build"
      PARENT_SCOPE)
    return()
  endif()

  set(${out_valid} TRUE PARENT_SCOPE)
  set(${out_reason} "" PARENT_SCOPE)
endfunction()

# Generate one profile-specific runtime target against an existing codec
# target. RUNTIME_NAME selects its public C namespace and permits multiple
# asymmetric runtimes to share CODEC_TARGET in one final image.
function(wirelink_wlc_generate_runtime)
  set(_options)
  set(_one_value_args
    TARGET
    CODEC_TARGET
    PROFILE
    OUTPUT_DIR
    RUNTIME_NAME)
  cmake_parse_arguments(WLC
    "${_options}" "${_one_value_args}" "" ${ARGN})

  if(WLC_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "wirelink_wlc_generate_runtime received unknown arguments: "
      "${WLC_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT WLC_TARGET)
    message(FATAL_ERROR "wirelink_wlc_generate_runtime requires TARGET")
  endif()
  if(TARGET "${WLC_TARGET}")
    message(FATAL_ERROR
      "wirelink_wlc_generate_runtime target '${WLC_TARGET}' already exists")
  endif()
  if(NOT WLC_CODEC_TARGET OR NOT TARGET "${WLC_CODEC_TARGET}")
    message(FATAL_ERROR
      "wirelink_wlc_generate_runtime requires an existing CODEC_TARGET")
  endif()
  if(NOT WLC_PROFILE)
    message(FATAL_ERROR "wirelink_wlc_generate_runtime requires PROFILE")
  endif()

  get_target_property(_schema "${WLC_CODEC_TARGET}" WIRELINK_WLC_SCHEMA)
  get_target_property(_wlc "${WLC_CODEC_TARGET}" WIRELINK_WLC_EXECUTABLE)
  get_target_property(_codec_codegen_target "${WLC_CODEC_TARGET}"
    WIRELINK_WLC_CODEGEN_TARGET)
  get_target_property(_codec_module "${WLC_CODEC_TARGET}"
    WIRELINK_WLC_CODEC_MODULE)
  if(NOT _schema OR NOT _wlc OR NOT _codec_codegen_target OR
      NOT _codec_module)
    message(FATAL_ERROR
      "CODEC_TARGET '${WLC_CODEC_TARGET}' was not created by "
      "wirelink_wlc_generate_codec")
  endif()

  get_filename_component(_profile "${WLC_PROFILE}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT EXISTS "${_profile}")
    message(FATAL_ERROR "Wirelink binding profile does not exist: ${_profile}")
  endif()
  if(WLC_RUNTIME_NAME)
    set(_runtime_name "${WLC_RUNTIME_NAME}")
  else()
    set(_runtime_name "${_codec_module}")
  endif()
  if(NOT _runtime_name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR
      "RUNTIME_NAME must be a portable C identifier, got '${_runtime_name}'")
  endif()

  if(WLC_OUTPUT_DIR)
    get_filename_component(_output_dir "${WLC_OUTPUT_DIR}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  else()
    set(_output_dir
      "${CMAKE_CURRENT_BINARY_DIR}/wirelink-generated/${WLC_TARGET}")
  endif()

  set(_generated
    "${_output_dir}/${_runtime_name}_runtime.h"
    "${_output_dir}/${_runtime_name}_runtime.c"
    "${_output_dir}/${_runtime_name}_runtime_manifest.json")
  set(_generated_source "${_output_dir}/${_runtime_name}_runtime.c")
  set(_manifest
    "${_output_dir}/${_runtime_name}_runtime_manifest.json")
  set(_codegen_stamp
    "${_output_dir}/.${_runtime_name}-wlc-runtime.stamp")
  set(_manifest_verifier
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WirelinkWlcVerifyManifest.cmake")
  add_custom_command(
    OUTPUT "${_codegen_stamp}"
    BYPRODUCTS ${_generated}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output_dir}"
    COMMAND "${_wlc}" compile-runtime "${_schema}"
      --profile "${_profile}"
      --runtime-name "${_runtime_name}"
      --out-dir "${_output_dir}"
    COMMAND "${CMAKE_COMMAND}"
      "-DWIRELINK_WLC_MANIFEST=${_manifest}"
      "-DWIRELINK_WLC_EXPECTED_VERSION=${WIRELINK_WLC_VERSION}"
      "-DWIRELINK_WLC_EXPECTED_ABI=${WIRELINK_WLC_CODEGEN_ABI}"
      -P "${_manifest_verifier}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_codegen_stamp}"
    DEPENDS
      "${_schema}"
      "${_profile}"
      "${_wlc}"
      "${_manifest_verifier}"
    COMMENT "Generating Wirelink runtime ${_runtime_name}"
    VERBATIM)

  set_source_files_properties("${_generated_source}" PROPERTIES GENERATED TRUE)
  add_custom_target("${WLC_TARGET}_wlc_codegen" DEPENDS "${_codegen_stamp}")
  add_dependencies("${WLC_TARGET}_wlc_codegen" "${_codec_codegen_target}")
  add_library("${WLC_TARGET}" STATIC "${_generated_source}")
  add_dependencies("${WLC_TARGET}" "${WLC_TARGET}_wlc_codegen")
  target_include_directories("${WLC_TARGET}" PUBLIC
    "$<BUILD_INTERFACE:${_output_dir}>")
  target_compile_features("${WLC_TARGET}" PUBLIC c_std_11)
  target_link_libraries("${WLC_TARGET}" PUBLIC "${WLC_CODEC_TARGET}")

  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_GENERATED_DIR "${_output_dir}")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_SCHEMA "${_schema}")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_PROFILE "${_profile}")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_EXECUTABLE "${_wlc}")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_CODEC_TARGET "${WLC_CODEC_TARGET}")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_RUNTIME_NAME "${_runtime_name}")
endfunction()

# Compatibility composition entry point. New integrations should retain the
# codec target and create one or more runtimes explicitly with the functions
# above.
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
  if(NOT WLC_PROFILE)
    wirelink_wlc_generate_codec(
      TARGET "${WLC_TARGET}"
      SCHEMA "${WLC_SCHEMA}"
      PREVIOUS "${WLC_PREVIOUS}"
      OUTPUT_DIR "${WLC_OUTPUT_DIR}"
      WLC_EXECUTABLE "${WLC_WLC_EXECUTABLE}")
    return()
  endif()

  set(_codec_target "${WLC_TARGET}_codec")
  wirelink_wlc_generate_codec(
    TARGET "${_codec_target}"
    SCHEMA "${WLC_SCHEMA}"
    PREVIOUS "${WLC_PREVIOUS}"
    OUTPUT_DIR "${WLC_OUTPUT_DIR}"
    WLC_EXECUTABLE "${WLC_WLC_EXECUTABLE}")
  wirelink_wlc_generate_runtime(
    TARGET "${WLC_TARGET}"
    CODEC_TARGET "${_codec_target}"
    PROFILE "${WLC_PROFILE}"
    OUTPUT_DIR "${WLC_OUTPUT_DIR}")
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
    set(_hash "5568a888dd9ff84b4ddce474b2e424d785d49c708d9c8ed5b08d2837de631ea0")
    set(_executable "wlc.exe")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
      _architecture STREQUAL "x86_64")
    set(_asset "wlc-linux-x86_64-musl.tar.gz")
    set(_hash "cdb168fe79fc4de720fddaa1ccb01e8cab38657b377dcb76fac6f3a6132673d4")
    set(_executable "wlc")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
      _architecture STREQUAL "aarch64")
    set(_asset "wlc-linux-aarch64-musl.tar.gz")
    set(_hash "8412165603dabc326965cecbb5355ff3a9f5cc61c5ec9f73219bbef601279977")
    set(_executable "wlc")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
      _architecture STREQUAL "x86_64")
    set(_asset "wlc-macos-x86_64.tar.gz")
    set(_hash "8f9b968a2d2187357b59b484453d4fd5bfd4147c9af01f7c0d84240eb66aa7e6")
    set(_executable "wlc")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
      _architecture STREQUAL "aarch64")
    set(_asset "wlc-macos-aarch64.tar.gz")
    set(_hash "1404ac1b885fff44afe1b0fe92da937b7832b2aba9813e34963d9b214a57ab96")
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

# Generate one schema-level codec/bindings target. WLC is a host tool even
# when the consuming target is cross compiled, so automatic discovery
# deliberately ignores CMAKE_FIND_ROOT_PATH.
function(wirelink_wlc_generate_codec)
  set(_options)
  set(_one_value_args
    TARGET
    SCHEMA
    PREVIOUS
    OUTPUT_DIR
    WLC_EXECUTABLE)
  cmake_parse_arguments(WLC
    "${_options}" "${_one_value_args}" "" ${ARGN})

  if(WLC_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "wirelink_wlc_generate_codec received unknown arguments: "
      "${WLC_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT WLC_TARGET)
    message(FATAL_ERROR "wirelink_wlc_generate_codec requires TARGET")
  endif()
  if(TARGET "${WLC_TARGET}")
    message(FATAL_ERROR
      "wirelink_wlc_generate_codec target '${WLC_TARGET}' already exists")
  endif()
  if(NOT WLC_SCHEMA)
    message(FATAL_ERROR "wirelink_wlc_generate_codec requires SCHEMA")
  endif()

  get_filename_component(_schema "${WLC_SCHEMA}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT EXISTS "${_schema}")
    message(FATAL_ERROR "Wirelink schema does not exist: ${_schema}")
  endif()
  get_filename_component(_module "${_schema}" NAME_WE)

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
  set(_command
    "${_wlc}" compile "${_schema}" --out-dir "${_output_dir}")
  set(_depends "${_schema}" "${_wlc}")
  if(WLC_PREVIOUS)
    list(APPEND _command --previous "${_previous}")
    list(APPEND _depends "${_previous}")
  endif()
  set(_codegen_stamp "${_output_dir}/.${_module}-wlc-codec.stamp")
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
    COMMENT "Generating Wirelink codec and bindings for ${_module}.wl"
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
      "wirelink_wlc_generate_codec requires the Wirelink::wirelink target")
  endif()

  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_GENERATED_DIR "${_output_dir}")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_SCHEMA "${_schema}")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_EXECUTABLE "${_wlc}")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_CODEGEN_STAMP "${_codegen_stamp}")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_CODEGEN_TARGET "${WLC_TARGET}_wlc_codegen")
  set_property(TARGET "${WLC_TARGET}" PROPERTY
    WIRELINK_WLC_CODEC_MODULE "${_module}")
endfunction()
