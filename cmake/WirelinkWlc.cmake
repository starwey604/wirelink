# SPDX-License-Identifier: Apache-2.0
include_guard(GLOBAL)
set(WIRELINK_WLC_VERSION "0.13.0" CACHE INTERNAL "Matching compiler" FORCE)
set(WIRELINK_WLC_CODEGEN_CONTRACT_MAJOR "0" CACHE INTERNAL "Current contract major" FORCE)
set(WIRELINK_WLC_CODEGEN_CONTRACT_MINOR "13" CACHE INTERNAL "Current contract minor" FORCE)
function(_wirelink_codegen_contract_accepts major minor result)
  if(major STREQUAL "0" AND minor STREQUAL "13")
    set(${result} TRUE PARENT_SCOPE)
  else()
    set(${result} FALSE PARENT_SCOPE)
  endif()
endfunction()
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
    COMMAND "${executable}" codegen-contract
    RESULT_VARIABLE _contract_result
    OUTPUT_VARIABLE _contract
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
    TIMEOUT 10)
  if(NOT _contract_result STREQUAL "0" OR
      NOT _contract MATCHES "^([0-9]+)\\.([0-9]+)$")
    set(${out_valid} FALSE PARENT_SCOPE)
    set(${out_reason}
      "did not report a MAJOR.MINOR generated-code contract; install the matching WLC build"
      PARENT_SCOPE)
    return()
  endif()
  _wirelink_codegen_contract_accepts("${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}"
    _contract_valid)
  if(NOT _contract_valid)
    set(${out_valid} FALSE PARENT_SCOPE)
    set(${out_reason}
      "provides generated-code contract ${_contract}, incompatible with ${WIRELINK_WLC_CODEGEN_CONTRACT_MAJOR}.${WIRELINK_WLC_CODEGEN_CONTRACT_MINOR}"
      PARENT_SCOPE)
    return()
  endif()

  set(${out_valid} TRUE PARENT_SCOPE)
  set(${out_reason} "" PARENT_SCOPE)
endfunction()

# Rust canonicalize() returns verbatim paths on Windows. CMake/MSBuild dependency
# tracking expects ordinary drive/UNC paths with forward slashes.
function(_wirelink_wlc_windows_dependency_path input out_path)
  string(REPLACE "\\" "/" _path "${input}")
  string(REGEX REPLACE "^//[?]/UNC/" "//" _path "${_path}")
  string(REGEX REPLACE "^//[?]/" "" _path "${_path}")
  set(${out_path} "${_path}" PARENT_SCOPE)
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
  if(CMAKE_HOST_WIN32)
    set(_normalized_inputs "")
    foreach(_input IN LISTS _inputs)
      _wirelink_wlc_windows_dependency_path("${_input}" _normalized)
      list(APPEND _normalized_inputs "${_normalized}")
    endforeach()
    set(_inputs "${_normalized_inputs}")
  endif()
  # Reconfigure when an import edge changes, then refresh the transitive set.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_inputs})
  set(${out_dependencies} "${_inputs}" PARENT_SCOPE)
endfunction()


# Use an explicitly selected or PATH-installed matching compiler; no fallback download.
function(_wirelink_wlc_resolve explicit result)
  if(explicit)
    set(compiler "${explicit}")
  elseif(WIRELINK_WLC_EXECUTABLE)
    set(compiler "${WIRELINK_WLC_EXECUTABLE}")
  else()
    find_program(compiler NAMES wlc NO_CACHE)
  endif()
  _wirelink_wlc_validate_executable("${compiler}" valid reason)
  if(NOT valid)
    message(FATAL_ERROR "Supply matching WLC 0.13.0 with WIRELINK_WLC_EXECUTABLE: ${reason}")
  endif()
  set(${result} "${compiler}" PARENT_SCOPE)
endfunction()
function(wirelink_wlc_generate_runtime)
  message(FATAL_ERROR "Separate legacy runtime generation was removed; use wirelink_wlc_generate with a profile v2")
endfunction()
function(wirelink_wlc_generate)
  cmake_parse_arguments(WLC "" "TARGET;SCHEMA;PROFILE;PREVIOUS;OUTPUT_DIR;WLC_EXECUTABLE" "PROFILES" ${ARGN})
  if(WLC_UNPARSED_ARGUMENTS OR NOT WLC_TARGET OR NOT WLC_SCHEMA OR TARGET "${WLC_TARGET}")
    message(FATAL_ERROR "wirelink_wlc_generate requires a new TARGET and SCHEMA; unknown arguments: ${WLC_UNPARSED_ARGUMENTS}")
  endif()
  _wirelink_wlc_resolve("${WLC_WLC_EXECUTABLE}" compiler)
  get_filename_component(schema "${WLC_SCHEMA}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  execute_process(COMMAND "${compiler}" module-name "${schema}"
    RESULT_VARIABLE module_status OUTPUT_VARIABLE module ERROR_VARIABLE module_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT module_status STREQUAL "0" OR NOT module MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "Cannot determine generated C module name: ${module_error}")
  endif()
  _wirelink_wlc_schema_dependencies("${compiler}" "${schema}" depends)
  list(APPEND depends "${compiler}")
  if(WLC_OUTPUT_DIR)
    get_filename_component(output "${WLC_OUTPUT_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  else()
    set(output "${CMAKE_CURRENT_BINARY_DIR}/wirelink-generated/${WLC_TARGET}")
  endif()
  set(command "${compiler}" compile "${schema}" --out-dir "${output}")
  set(profiles ${WLC_PROFILES})
  if(WLC_PROFILE)
    list(APPEND profiles "${WLC_PROFILE}")
  endif()
  foreach(profile IN LISTS profiles)
    get_filename_component(profile "${profile}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND command --profile "${profile}")
    list(APPEND depends "${profile}")
  endforeach()
  if(WLC_PREVIOUS)
    get_filename_component(previous "${WLC_PREVIOUS}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND command --previous "${previous}")
    _wirelink_wlc_schema_dependencies("${compiler}" "${previous}" previous_depends)
    list(APPEND depends ${previous_depends})
  endif()
  set(generated "${output}/${module}.c" "${output}/${module}.h" "${output}/${module}_values.h")
  set(sources "${output}/${module}.c")
  if(profiles)
    list(APPEND generated "${output}/${module}_kernel.c" "${output}/${module}_kernel.h"
      "${output}/${module}_kernel_codecs.h" "${output}/${module}_kernel_deployment.json")
    list(APPEND sources "${output}/${module}_kernel.c")
    set(manifest "${output}/${module}_kernel_manifest.json")
    # The compiler's explicit deployment chooses exactly one connector. Compile
    # it only when emitted; this CMake selection is not a runtime dispatcher.
    execute_process(COMMAND ${command} RESULT_VARIABLE status ERROR_VARIABLE error OUTPUT_QUIET)
    if(NOT status STREQUAL "0")
      message(FATAL_ERROR "WLC profile validation/generation failed: ${error}")
    endif()
    if(EXISTS "${output}/${module}_kernel_transport.c")
      list(APPEND sources "${output}/${module}_kernel_transport.c")
      list(APPEND generated "${output}/${module}_kernel_transport.c" "${output}/${module}_kernel_transport.h")
      file(READ "${output}/${module}_kernel_deployment.json" deployment)
      string(JSON envelope GET "${deployment}" envelope)
      if(envelope STREQUAL "cobs_stream")
        set(connector Wirelink::stream_cobs_crc16)
      else()
        set(connector Wirelink::stream_length16_none)
      endif()
    endif()
  else()
    set(manifest "${output}/${module}_manifest.json")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${profiles})
  set(verifier "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WirelinkWlcVerifyManifest.cmake")
  add_custom_command(OUTPUT ${generated} "${manifest}"
    COMMAND ${command}
    COMMAND "${CMAKE_COMMAND}" "-DWIRELINK_WLC_MANIFEST=${manifest}"
      "-DWIRELINK_WLC_EXPECTED_VERSION=${WIRELINK_WLC_VERSION}"
      "-DWIRELINK_WLC_EXPECTED_CONTRACT_MAJOR=0" "-DWIRELINK_WLC_EXPECTED_CONTRACT_MINOR=13"
      -P "${verifier}"
    DEPENDS ${depends} "${verifier}" VERBATIM COMMAND_EXPAND_LISTS)
  add_library(${WLC_TARGET} STATIC ${sources})
  target_include_directories(${WLC_TARGET} PUBLIC "${output}")
  target_compile_features(${WLC_TARGET} PUBLIC c_std_11)
  if(TARGET Wirelink::kernel)
    if(profiles)
      target_link_libraries(${WLC_TARGET} PUBLIC Wirelink::kernel ${connector})
    else()
      target_link_libraries(${WLC_TARGET} PUBLIC Wirelink::types)
    endif()
  elseif(DEFINED ZEPHYR_BASE AND TARGET wirelink)
    if(profiles AND NOT CONFIG_WIRELINK)
      message(FATAL_ERROR "Generated kernel requires CONFIG_WIRELINK=y")
    endif()
    if(connector STREQUAL "Wirelink::stream_cobs_crc16" AND NOT CONFIG_WIRELINK_STREAM_COBS_CRC16)
      message(FATAL_ERROR "COBS profile requires CONFIG_WIRELINK_STREAM_COBS_CRC16=y")
    elseif(connector STREQUAL "Wirelink::stream_length16_none" AND NOT CONFIG_WIRELINK_STREAM_LENGTH16_NONE)
      message(FATAL_ERROR "Length16 profile requires CONFIG_WIRELINK_STREAM_LENGTH16_NONE=y")
    endif()
    target_link_libraries(${WLC_TARGET} PUBLIC wirelink zephyr_interface)
  else()
    message(FATAL_ERROR "Load the Wirelink core package or Zephyr module before generating C")
  endif()
  set_property(TARGET ${WLC_TARGET} PROPERTY WIRELINK_WLC_GENERATED_DIR "${output}")
  set_property(TARGET ${WLC_TARGET} PROPERTY WIRELINK_WLC_SCHEMA "${schema}")
  set_property(TARGET ${WLC_TARGET} PROPERTY WIRELINK_WLC_EXECUTABLE "${compiler}")
endfunction()
function(wirelink_wlc_generate_codec)
  cmake_parse_arguments(C "" "PROFILE" "PROFILES" ${ARGN})
  if(C_PROFILE OR C_PROFILES)
    message(FATAL_ERROR "Use wirelink_wlc_generate for a profile-specific endpoint; codec-only generation has no execution profile")
  endif()
  wirelink_wlc_generate(${ARGN})
endfunction()
