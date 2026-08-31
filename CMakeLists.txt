cmake_minimum_required(VERSION 3.21)

project(wirelink VERSION 0.9.0 LANGUAGES C)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(WIRELINK_SOURCES
  src/cobs.c
  src/crc.c
  src/frame.c
  src/init.c
  src/protocol.c
  src/rx_ring_bipbuf.c
  src/transport.c
  src/types.c
  src/unit_rx.c
)

add_library(wirelink ${WIRELINK_SOURCES})
add_library(Wirelink::wirelink ALIAS wirelink)

target_include_directories(wirelink PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
set_target_properties(wirelink PROPERTIES EXPORT_NAME wirelink)

option(WIRELINK_BUILD_FUZZERS
  "Build Clang libFuzzer targets" OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink PRIVATE -Wall -Wextra -Wpedantic)
  # Sanitizer instrumentation adds artificial compiler-managed stack state,
  # so enforce the embedded frame budget only on regular builds.
  string(CONCAT _wirelink_c_flags
    "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG} ${CMAKE_C_FLAGS_RELEASE} "
    "${CMAKE_C_FLAGS_RELWITHDEBINFO} ${CMAKE_C_FLAGS_MINSIZEREL}")
  if(NOT WIRELINK_BUILD_FUZZERS AND NOT _wirelink_c_flags MATCHES "-fsanitize")
    # wl_frame_encode() and wl_poll() run on small embedded stacks. Keep their
    # translation units' fixed frames within a predictable hot-path budget.
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set_property(SOURCE src/frame.c APPEND PROPERTY COMPILE_OPTIONS
        -Werror=frame-larger-than=256)
      set_property(SOURCE src/protocol.c APPEND PROPERTY COMPILE_OPTIONS
        -Werror=frame-larger-than=256)
    else()
      set_property(SOURCE src/frame.c APPEND PROPERTY COMPILE_OPTIONS
        -Wframe-larger-than=256
        -Werror=frame-larger-than)
      set_property(SOURCE src/protocol.c APPEND PROPERTY COMPILE_OPTIONS
        -Wframe-larger-than=256
        -Werror=frame-larger-than)
    endif()
  endif()
  unset(_wirelink_c_flags)
endif()

option(WIRELINK_BUILD_ASTRIAL_ADAPTER
  "Build the optional Astrial desktop serial adapter" OFF)
option(WIRELINK_BUILD_ASTRIAL_USB_ADAPTER
  "Build the optional Astrial USB bulk adapter" OFF)
option(WIRELINK_BUILD_HOST_BENCHMARKS
  "Build host USB Bulk and CDC latency benchmarks" OFF)
option(WIRELINK_BUILD_ASIO_UDP_ADAPTER
  "Build the optional standalone-Asio UDP adapter" OFF)
option(WIRELINK_BUILD_EXAMPLES
  "Build Wirelink usage examples" ${PROJECT_IS_TOP_LEVEL})
option(WIRELINK_INSTALL
  "Generate Wirelink install and package metadata" ${PROJECT_IS_TOP_LEVEL})
set(WIRELINK_ASTRIAL_SOURCE_DIR "" CACHE PATH
  "Astrial source tree used when no astrial target is already available")
set(WIRELINK_ASIO_INCLUDE_DIR "" CACHE PATH
  "Directory containing standalone Asio's asio.hpp")

if(WIRELINK_BUILD_HOST_BENCHMARKS)
  set(WIRELINK_BUILD_ASTRIAL_ADAPTER ON)
  set(WIRELINK_BUILD_ASTRIAL_USB_ADAPTER ON)
endif()

if(WIRELINK_BUILD_ASTRIAL_ADAPTER OR WIRELINK_BUILD_ASTRIAL_USB_ADAPTER)
  enable_language(CXX)
  if(NOT TARGET astrial)
    if(WIRELINK_ASTRIAL_SOURCE_DIR STREQUAL "")
      message(FATAL_ERROR
        "WIRELINK_BUILD_ASTRIAL_ADAPTER requires an existing astrial target "
        "or WIRELINK_ASTRIAL_SOURCE_DIR")
    endif()
    set(_WIRELINK_SAVED_BUILD_EXAMPLE "${BUILD_EXAMPLE}")
    set(_WIRELINK_SAVED_BUILD_TESTING "${BUILD_TESTING}")
    set(_WIRELINK_SAVED_ASTRIAL_BUILD_USB "${ASTRIAL_BUILD_USB}")
    set(BUILD_EXAMPLE OFF)
    set(BUILD_TESTING OFF)
    if(WIRELINK_BUILD_ASTRIAL_USB_ADAPTER)
      set(ASTRIAL_BUILD_USB ON)
    endif()
    add_subdirectory("${WIRELINK_ASTRIAL_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/_deps/astrial" EXCLUDE_FROM_ALL)
    set(BUILD_EXAMPLE "${_WIRELINK_SAVED_BUILD_EXAMPLE}")
    set(BUILD_TESTING "${_WIRELINK_SAVED_BUILD_TESTING}")
    set(ASTRIAL_BUILD_USB "${_WIRELINK_SAVED_ASTRIAL_BUILD_USB}")
    unset(_WIRELINK_SAVED_BUILD_EXAMPLE)
    unset(_WIRELINK_SAVED_BUILD_TESTING)
    unset(_WIRELINK_SAVED_ASTRIAL_BUILD_USB)
  endif()
endif()

if(WIRELINK_BUILD_ASTRIAL_ADAPTER)
  add_subdirectory(adapters/astrial)
endif()

if(WIRELINK_BUILD_ASTRIAL_USB_ADAPTER)
  if(NOT TARGET astrial_usb)
    message(FATAL_ERROR
      "WIRELINK_BUILD_ASTRIAL_USB_ADAPTER requires the Astrial USB target")
  endif()
  add_subdirectory(adapters/astrial_usb)
endif()

if(WIRELINK_BUILD_ASIO_UDP_ADAPTER)
  enable_language(CXX)
  add_subdirectory(adapters/asio_udp)
endif()

if(WIRELINK_BUILD_HOST_BENCHMARKS)
  add_subdirectory(benchmarks/host)
endif()

if(WIRELINK_BUILD_FUZZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "WIRELINK_BUILD_FUZZERS requires Clang")
  endif()
  target_compile_options(wirelink PRIVATE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer)
  add_subdirectory(fuzz)
endif()

if(WIRELINK_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(WIRELINK_INSTALL)
  install(TARGETS wirelink
    EXPORT WirelinkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(FILES
    include/wirelink/cobs.h
    include/wirelink/codec.h
    include/wirelink/crc.h
    include/wirelink/frame.h
    include/wirelink/span.h
    include/wirelink/types.h
    include/wirelink/version.h
    include/wirelink/wirelink.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink)

  install(EXPORT WirelinkTargets
    FILE WirelinkTargets.cmake
    NAMESPACE Wirelink::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Wirelink)

  configure_package_config_file(
    cmake/WirelinkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/WirelinkConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Wirelink)
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/WirelinkConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
  configure_file(cmake/wirelink.pc.in wirelink.pc @ONLY)

  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/WirelinkConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/WirelinkConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Wirelink)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wirelink.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/wirelink)
endif()
