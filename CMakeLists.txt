cmake_minimum_required(VERSION 3.21)

project(wirelink VERSION 0.7.0 LANGUAGES C)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(WirelinkWlc)

set(WIRELINK_SOURCES
  src/bulk_receiver.c
  src/bulk_sender.c
  src/cobs.c
  src/crc.c
  src/endpoint.c
  src/fifo.c
  src/frame.c
  src/init.c
  src/latest.c
  src/outbox.c
  src/pump.c
  src/protocol.c
  src/rpc.c
  src/rpc_async.c
  src/rx_ring_bipbuf.c
  src/schedule.c
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

# The portable in-memory adapter is a separate target so embedded consumers
# pay no code or state cost unless they explicitly link it.
add_subdirectory(adapters/loopback)
add_subdirectory(diagnostics)

if(MSVC)
  target_compile_options(wirelink PUBLIC /utf-8)
  target_compile_options(wirelink PRIVATE /experimental:c11atomics)
endif()

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
      set_property(SOURCE src/bulk_receiver.c APPEND PROPERTY COMPILE_OPTIONS
        -Werror=frame-larger-than=256)
      set_property(SOURCE src/bulk_sender.c APPEND PROPERTY COMPILE_OPTIONS
        -Werror=frame-larger-than=256)
    else()
      set_property(SOURCE src/frame.c APPEND PROPERTY COMPILE_OPTIONS
        -Wframe-larger-than=256
        -Werror=frame-larger-than)
      set_property(SOURCE src/protocol.c APPEND PROPERTY COMPILE_OPTIONS
        -Wframe-larger-than=256
        -Werror=frame-larger-than)
      set_property(SOURCE src/bulk_receiver.c APPEND PROPERTY COMPILE_OPTIONS
        -Wframe-larger-than=256
        -Werror=frame-larger-than)
      set_property(SOURCE src/bulk_sender.c APPEND PROPERTY COMPILE_OPTIONS
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
option(WIRELINK_BUILD_FIFO_BENCHMARK
  "Build the host SPSC FIFO and command-flow benchmark" OFF)
option(WIRELINK_BUILD_BULK_BENCHMARK
  "Build the host sequential bulk runtime benchmark" OFF)
option(WIRELINK_BUILD_RPC_PEER_BENCHMARK
  "Build the host generated-RPC peer fast-path benchmark" OFF)
option(WIRELINK_BUILD_API_BENCHMARKS
  "Build generated endpoint CPU and localhost UDP regression benchmarks" OFF)
option(WIRELINK_BUILD_FRAMING_BENCHMARK
  "Build frame encoding and TX retry CPU benchmarks (Google Benchmark)" OFF)
option(WIRELINK_BUILD_RPC_VALIDATION_BENCHMARK
  "Build generated request validation/conversion CPU benchmarks" OFF)
option(WIRELINK_BUILD_EXECUTOR_BENCHMARK
  "Build multithreaded executor contention benchmarks (enables API benchmarks)" OFF)
option(WIRELINK_BUILD_OWNER_PASS_TESTS
  "Build deterministic owner-pass accounting workloads (requires host activity counters)" OFF)
option(WIRELINK_BUILD_ASIO_UDP_ADAPTER
  "Build the optional standalone-Asio UDP adapter" OFF)
option(WIRELINK_BUILD_HOST_RUNTIME
  "Build the optional C++20 threaded session runtime" OFF)
option(WIRELINK_HOST_PROFILING
  "Enable bounded desktop timing diagnostics (no hot-path logging)" OFF)
option(WIRELINK_HOST_LOCK_PROFILING
  "Measure mutex wall times and notification latency without broad CPU probes" OFF)
if(WIRELINK_HOST_PROFILING)
  target_compile_definitions(wirelink INTERFACE WIRELINK_HOST_PROFILING=1)
endif()
if(WIRELINK_HOST_LOCK_PROFILING)
  target_compile_definitions(wirelink INTERFACE WIRELINK_HOST_LOCK_PROFILING=1)
endif()
option(WIRELINK_BUILD_STORAGE
  "Build the optional fixed-block endpoint allocator" OFF)
option(WIRELINK_BUILD_EXAMPLES
  "Build Wirelink usage examples" ${PROJECT_IS_TOP_LEVEL})
option(WIRELINK_BUILD_GETTING_STARTED
  "Build the WLC-generated UDP tutorials (requires compatible WLC and Asio)" OFF)
option(WIRELINK_BUILD_COMPOSED_SERVICES
  "Build the static RPC/telemetry/bulk composition example (requires matching WLC)" OFF)
option(WIRELINK_BUILD_DOCS
  "Build the Astro/Starlight documentation site" OFF)
option(WIRELINK_INSTALL
  "Generate Wirelink install and package metadata" ${PROJECT_IS_TOP_LEVEL})
set(WIRELINK_ASTRIAL_SOURCE_DIR "" CACHE PATH
  "Astrial source tree used when no astrial target is already available")
set(WIRELINK_ASIO_INCLUDE_DIR "" CACHE PATH
  "Directory containing standalone Asio's asio.hpp")

set(_wirelink_platform_default OFF)
if(PROJECT_IS_TOP_LEVEL AND CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin|Windows)$")
  set(_wirelink_platform_default ON)
endif()
option(WIRELINK_BUILD_PLATFORM "Build the native clock/session provider" ${_wirelink_platform_default})
unset(_wirelink_platform_default)
if(WIRELINK_BUILD_PLATFORM OR WIRELINK_BUILD_GETTING_STARTED)
  add_subdirectory(runtime/platform)
endif()

if(WIRELINK_BUILD_STORAGE)
  add_subdirectory(runtime/storage)
endif()

if(WIRELINK_BUILD_EXECUTOR_BENCHMARK)
  set(WIRELINK_BUILD_API_BENCHMARKS ON)
  set(WIRELINK_BUILD_HOST_RUNTIME ON)
endif()
if(WIRELINK_BUILD_HOST_RUNTIME)
  enable_language(CXX)
  add_subdirectory(runtime/host)
endif()

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
    set(_WIRELINK_SAVED_ASTRIAL_BUILD_EXAMPLES
      "${ASTRIAL_BUILD_EXAMPLES}")
    set(_WIRELINK_SAVED_ASTRIAL_BUILD_TESTS "${ASTRIAL_BUILD_TESTS}")
    set(_WIRELINK_SAVED_ASTRIAL_BUILD_USB "${ASTRIAL_BUILD_USB}")
    set(ASTRIAL_BUILD_EXAMPLES OFF)
    set(ASTRIAL_BUILD_TESTS OFF)
    if(WIRELINK_BUILD_ASTRIAL_USB_ADAPTER)
      set(ASTRIAL_BUILD_USB ON)
    endif()
    add_subdirectory("${WIRELINK_ASTRIAL_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/_deps/astrial" EXCLUDE_FROM_ALL)
    set(ASTRIAL_BUILD_EXAMPLES
      "${_WIRELINK_SAVED_ASTRIAL_BUILD_EXAMPLES}")
    set(ASTRIAL_BUILD_TESTS "${_WIRELINK_SAVED_ASTRIAL_BUILD_TESTS}")
    set(ASTRIAL_BUILD_USB "${_WIRELINK_SAVED_ASTRIAL_BUILD_USB}")
    unset(_WIRELINK_SAVED_ASTRIAL_BUILD_EXAMPLES)
    unset(_WIRELINK_SAVED_ASTRIAL_BUILD_TESTS)
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

if(WIRELINK_BUILD_GETTING_STARTED OR WIRELINK_BUILD_API_BENCHMARKS)
  set(WIRELINK_BUILD_ASIO_UDP_ADAPTER ON)
endif()

if(WIRELINK_BUILD_ASIO_UDP_ADAPTER)
  enable_language(CXX)
  add_subdirectory(adapters/asio_udp)
endif()

if(WIRELINK_BUILD_HOST_BENCHMARKS)
  add_subdirectory(benchmarks/host)
endif()

if(WIRELINK_BUILD_FIFO_BENCHMARK)
  add_subdirectory(benchmarks/fifo)
endif()

if(WIRELINK_BUILD_BULK_BENCHMARK)
  add_subdirectory(benchmarks/bulk)
endif()

if(WIRELINK_BUILD_RPC_PEER_BENCHMARK)
  add_subdirectory(benchmarks/rpc_peer)
endif()

if(WIRELINK_BUILD_API_BENCHMARKS)
  add_subdirectory(benchmarks/api)
endif()
if(WIRELINK_BUILD_OWNER_PASS_TESTS)
  add_subdirectory(benchmarks/owner_pass)
endif()

if(WIRELINK_BUILD_FUZZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "WIRELINK_BUILD_FUZZERS requires Clang")
  endif()
  target_compile_options(wirelink PRIVATE
    # Instrument the core as well as the harness so fuzz feedback explores
    # frame/protocol branches, not only the oracle's success/error checks.
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-omit-frame-pointer)
  # Any executable consuming the instrumented static archive must also link
  # the sanitizer runtimes, including the examples built by default.
  target_link_options(wirelink INTERFACE
    -fsanitize=address,undefined)
  add_subdirectory(fuzz)
endif()

if(WIRELINK_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(WIRELINK_BUILD_DOCS)
  find_program(WIRELINK_NPM_EXECUTABLE npm REQUIRED)
  set(_wirelink_site_dir "${PROJECT_SOURCE_DIR}/site")
  set(_wirelink_docs_deps_stamp
    "${CMAKE_CURRENT_BINARY_DIR}/wirelink-docs-deps.stamp")

  add_custom_command(
    OUTPUT "${_wirelink_docs_deps_stamp}"
    COMMAND "${WIRELINK_NPM_EXECUTABLE}" ci
    COMMAND "${CMAKE_COMMAND}" -E touch "${_wirelink_docs_deps_stamp}"
    DEPENDS
      "${_wirelink_site_dir}/package.json"
      "${_wirelink_site_dir}/package-lock.json"
    WORKING_DIRECTORY "${_wirelink_site_dir}"
    COMMENT "Installing Wirelink documentation dependencies"
    VERBATIM)

  add_custom_target(wirelink_docs
    COMMAND "${WIRELINK_NPM_EXECUTABLE}" run build
    COMMAND "${CMAKE_COMMAND}" -E remove_directory
      "${CMAKE_CURRENT_BINARY_DIR}/docs"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${_wirelink_site_dir}/dist" "${CMAKE_CURRENT_BINARY_DIR}/docs"
    DEPENDS "${_wirelink_docs_deps_stamp}"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/docs/wirelink/index.html"
    WORKING_DIRECTORY "${_wirelink_site_dir}"
    COMMENT "Building Wirelink documentation"
    USES_TERMINAL
    VERBATIM)

  unset(_wirelink_docs_deps_stamp)
  unset(_wirelink_site_dir)
endif()

if(WIRELINK_BUILD_FRAMING_BENCHMARK)
  enable_language(CXX)
  add_subdirectory(benchmarks/framing)
endif()

if(WIRELINK_BUILD_RPC_VALIDATION_BENCHMARK)
  enable_language(CXX)
  add_subdirectory(benchmarks/rpc_validation)
endif()

if(WIRELINK_INSTALL)
  install(FILES include/wirelink/detail/usb_stream_rx.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink/detail)
  install(TARGETS wirelink
    EXPORT WirelinkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(TARGETS wirelink_loopback
    EXPORT WirelinkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(TARGETS wirelink_diagnostics
    EXPORT WirelinkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  if(TARGET wirelink_host)
    install(TARGETS wirelink_host
      EXPORT WirelinkTargets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES runtime/host/include/wirelink/host/executor.hpp
      runtime/host/include/wirelink/host/executor_activity.hpp
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink/host)
    install(FILES include/wirelink/detail/coalescing_event.hpp
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink/detail)
    install(FILES include/wirelink/diagnostics/host_profile.hpp
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink/diagnostics)
  endif()

  if(TARGET wirelink_asio_udp OR TARGET wirelink_host)
    install(FILES runtime/host/include/wirelink/host/clock.hpp
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink/host)
  endif()
  if(TARGET wirelink_asio_udp)
    install(TARGETS wirelink_asio_udp EXPORT WirelinkTargets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES adapters/asio_udp/include/wirelink/asio/udp_adapter.hpp
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink/asio)
  endif()

  install(FILES
    include/wirelink/adapter.h
    include/wirelink/alignment.h
    include/wirelink/bulk.h
    include/wirelink/clock.h
    include/wirelink/environment.h
    include/wirelink/platform.h
    include/wirelink/cobs.h
    include/wirelink/codec.h
    include/wirelink/crc.h
    include/wirelink/diagnostics.h
    include/wirelink/endpoint.h
    include/wirelink/fifo.h
    include/wirelink/frame.h
    include/wirelink/latest.h
    include/wirelink/link.h
    include/wirelink/loopback.h
    include/wirelink/outbox.h
    include/wirelink/port.h
    include/wirelink/profile.h
    include/wirelink/pump.h
    include/wirelink/rpc.h
    include/wirelink/rpc_async.h
    include/wirelink/rpc_result.h
    include/wirelink/rpc_sync.h
    include/wirelink/allocator.h
    include/wirelink/span.h
    include/wirelink/types.h
    include/wirelink/version.h
    include/wirelink/wait.h
    include/wirelink/wirelink.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink)

  install(EXPORT WirelinkTargets
    FILE WirelinkTargets.cmake
    NAMESPACE Wirelink::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Wirelink)

  set(WIRELINK_PACKAGE_HAS_HOST_RUNTIME OFF)
  set(WIRELINK_PACKAGE_HAS_ASIO_UDP_ADAPTER OFF)
  if(TARGET wirelink_asio_udp)
    set(WIRELINK_PACKAGE_HAS_ASIO_UDP_ADAPTER ON)
  endif()
  if(TARGET wirelink_host)
    set(WIRELINK_PACKAGE_HAS_HOST_RUNTIME ON)
  endif()
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
    cmake/WirelinkWlc.cmake
    cmake/WirelinkWlcBootstrap.cmake
    cmake/WirelinkWlcVerifyManifest.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Wirelink)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wirelink.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/wirelink)
endif()
