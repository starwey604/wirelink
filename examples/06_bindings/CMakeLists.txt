# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.21)
project(CalculatorSdk VERSION 0.8.0 LANGUAGES C CXX)
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Wirelink 0.8.0 EXACT CONFIG REQUIRED)
foreach(dependency cpp_host platform)
  if(NOT TARGET Wirelink::${dependency})
    message(FATAL_ERROR "Install Wirelink with CPP_BINDINGS and PLATFORM enabled")
  endif()
endforeach()

# Checked-in C artifacts make the source distribution independent of a WLC
# checkout/compiler. Verify the paired ABI before compiling any generated C.
set(WIRELINK_WLC_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/generated/calculator_manifest.json")
set(WIRELINK_WLC_EXPECTED_VERSION "${WIRELINK_WLC_VERSION}")
set(WIRELINK_WLC_EXPECTED_ABI "${WIRELINK_WLC_CODEGEN_ABI}")
include("${Wirelink_DIR}/WirelinkWlcVerifyManifest.cmake")

add_library(calculator_client STATIC src/client.cpp src/endpoint.c generated/calculator.c
  generated/calculator_bindings.c generated/calculator_runtime.c)
add_library(CalculatorSdk::client ALIAS calculator_client)
set_target_properties(calculator_client PROPERTIES EXPORT_NAME client
  POSITION_INDEPENDENT_CODE ON C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
  CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_compile_features(calculator_client PUBLIC cxx_std_20)
target_include_directories(calculator_client PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  PRIVATE generated)
target_link_libraries(calculator_client PUBLIC Wirelink::cpp_host PRIVATE Wirelink::platform)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(calculator_client PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS calculator_client EXPORT CalculatorSdkTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT development)
install(EXPORT CalculatorSdkTargets NAMESPACE CalculatorSdk::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CalculatorSdk COMPONENT development)
configure_package_config_file(cmake/CalculatorSdkConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/CalculatorSdkConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CalculatorSdk)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/CalculatorSdkConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/CalculatorSdkConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/CalculatorSdkConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CalculatorSdk COMPONENT development)

option(CALCULATOR_BUILD_PYTHON "Build the calculator Python extension" OFF)
if(CALCULATOR_BUILD_PYTHON)
  find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
  execute_process(COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_VARIABLE _nanobind_dir OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  find_package(nanobind 3 CONFIG REQUIRED PATHS "${_nanobind_dir}")
  foreach(dependency wirelink host asio_udp platform cpp_host)
    get_target_property(_type Wirelink::${dependency} TYPE)
    if(NOT _type STREQUAL "STATIC_LIBRARY")
      message(FATAL_ERROR "The self-contained SDK wheel requires static Wirelink::${dependency}")
    endif()
  endforeach()
  nanobind_add_module(calculator_native NB_STATIC NB_DOMAIN calculator_sdk src/python.cpp)
  set_target_properties(calculator_native PROPERTIES OUTPUT_NAME _native)
  target_link_libraries(calculator_native PRIVATE CalculatorSdk::client)
  if(UNIX AND NOT APPLE)
    # Do not expose static Wirelink/WLC symbols to other independently built SDKs.
    target_link_options(calculator_native PRIVATE "LINKER:--exclude-libs,ALL")
  endif()
  install(TARGETS calculator_native LIBRARY DESTINATION calculator_sdk COMPONENT python
    RUNTIME DESTINATION calculator_sdk COMPONENT python)
endif()

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory(tests)
endif()
