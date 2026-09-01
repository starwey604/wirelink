include_guard(GLOBAL)

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

  if(WLC_WLC_EXECUTABLE)
    get_filename_component(_wlc "${WLC_WLC_EXECUTABLE}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    if(NOT EXISTS "${_wlc}")
      message(FATAL_ERROR "WLC executable does not exist: ${_wlc}")
    endif()
  elseif(WIRELINK_WLC_EXECUTABLE)
    get_filename_component(_wlc "${WIRELINK_WLC_EXECUTABLE}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    if(NOT EXISTS "${_wlc}")
      message(FATAL_ERROR "WIRELINK_WLC_EXECUTABLE does not exist: ${_wlc}")
    endif()
  else()
    find_program(_wlc NAMES wlc NO_CMAKE_FIND_ROOT_PATH)
    if(NOT _wlc)
      message(FATAL_ERROR
        "wlc was not found on the host PATH; set WLC_EXECUTABLE for this "
        "call or WIRELINK_WLC_EXECUTABLE for the project")
    endif()
  endif()

  set(_generated
    "${_output_dir}/${_module}.h"
    "${_output_dir}/${_module}.c"
    "${_output_dir}/${_module}_bindings.h"
    "${_output_dir}/${_module}_bindings.c"
    "${_output_dir}/${_module}_manifest.json")
  set(_generated_sources
    "${_output_dir}/${_module}.c"
    "${_output_dir}/${_module}_bindings.c")
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

  add_custom_command(
    OUTPUT ${_generated}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output_dir}"
    COMMAND ${_command}
    DEPENDS ${_depends}
    COMMENT "Generating Wirelink C bindings for ${_module}.wl"
    COMMAND_EXPAND_LISTS
    VERBATIM)

  add_library("${WLC_TARGET}" STATIC ${_generated_sources})
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
  if(WLC_PROFILE)
    set_property(TARGET "${WLC_TARGET}" PROPERTY
      WIRELINK_WLC_PROFILE "${_profile}")
  endif()
endfunction()
