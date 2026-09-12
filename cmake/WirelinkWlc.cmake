include_guard(GLOBAL)

# These values are consumed by functions that may be called from a parent
# directory after Wirelink itself was added with add_subdirectory(). Keep them
# in the global CMake cache so function call-site scope cannot hide them.
set(WIRELINK_WLC_VERSION "0.7.0-rc.1" CACHE INTERNAL
  "Pinned WLC host compiler version" FORCE)
set(WIRELINK_WLC_CODEGEN_ABI "32" CACHE INTERNAL
  "Pinned WLC generated-code ABI" FORCE)
option(WIRELINK_WLC_AUTO_DOWNLOAD
  "Fetch verified WLC host tools, or build pinned source on other hosts" ON)
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

# Resolve imported files at configure time and refresh edges after edits.
function(_wirelink_wlc_schema_dependencies executable schema out_dependencies)
  execute_process(COMMAND "${executable}" dependencies "${schema}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _inputs ERROR_VARIABLE _error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "Cannot resolve Wirelink schema imports: ${_error}")
  endif()
  string(REPLACE "\r\n" "\n" _inputs "${_inputs}")
  string(REPLACE "\n" ";" _inputs "${_inputs}")
  # Reconfigure when an import edge changes, then refresh the transitive set.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_inputs})
  set(${out_dependencies} "${_inputs}" PARENT_SCOPE)
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
    "${_options}" "${_one_value_args}" "PROFILES" ${ARGN})

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
  if(WLC_PROFILE AND WLC_PROFILES)
    message(FATAL_ERROR "Use PROFILE or PROFILES, not both")
  endif()
  if(NOT WLC_PROFILE AND NOT WLC_PROFILES)
    message(FATAL_ERROR "wirelink_wlc_generate_runtime requires PROFILE or PROFILES")
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

  set(_profiles)
  set(_profile_args)
  foreach(_input IN LISTS WLC_PROFILE WLC_PROFILES)
    get_filename_component(_profile "${_input}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${_profile}")
      message(FATAL_ERROR "Wirelink binding profile does not exist: ${_profile}")
    endif()
    list(APPEND _profiles "${_profile}")
    list(APPEND _profile_args --profile "${_profile}")
  endforeach()
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
    "${_output_dir}/${_runtime_name}_endpoint.h"
    "${_output_dir}/${_runtime_name}_advanced.h"
    "${_output_dir}/${_runtime_name}_runtime.c"
    "${_output_dir}/${_runtime_name}_runtime_manifest.json")
  set(_generated_source "${_output_dir}/${_runtime_name}_runtime.c")
  _wirelink_wlc_schema_dependencies("${_wlc}" "${_schema}" _schema_dependencies)
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
      ${_profile_args}
      --runtime-name "${_runtime_name}"
      --out-dir "${_output_dir}"
    COMMAND "${CMAKE_COMMAND}"
      "-DWIRELINK_WLC_MANIFEST=${_manifest}"
      "-DWIRELINK_WLC_EXPECTED_VERSION=${WIRELINK_WLC_VERSION}"
      "-DWIRELINK_WLC_EXPECTED_ABI=${WIRELINK_WLC_CODEGEN_ABI}"
      -P "${_manifest_verifier}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_codegen_stamp}"
    DEPENDS
      ${_schema_dependencies}
      ${_profiles}
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
    WIRELINK_WLC_PROFILE "${_profiles}")
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

include("${CMAKE_CURRENT_LIST_DIR}/WirelinkWlcBootstrap.cmake")

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

  _wirelink_wlc_bootstrap(_wlc)
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
    "${_output_dir}/${_module}_values.h"
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
  _wirelink_wlc_schema_dependencies("${_wlc}" "${_schema}" _schema_dependencies)
  set(_depends ${_schema_dependencies} "${_wlc}")
  if(WLC_PREVIOUS)
    list(APPEND _command --previous "${_previous}")
    _wirelink_wlc_schema_dependencies("${_wlc}" "${_previous}" _previous_dependencies)
    list(APPEND _depends ${_previous_dependencies})
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
