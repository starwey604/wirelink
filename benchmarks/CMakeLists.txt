# SPDX-License-Identifier: Apache-2.0
# The report and parser tests need only Python, independently of native benchmarks.
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_subdirectory(report)
  add_test(NAME benchmark_bench_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_bench.py")
  set_tests_properties(benchmark_bench_tests PROPERTIES
    TIMEOUT 30 LABELS "benchmark;bench")

  # Explicit standalone parsers; the real mixed model's argument-dependent
  # tests remain registered by its own CMake target.
  foreach(_script reliable_loss/test_compare.py footprint/test_compare.py
      api/test_endpoint_reports.py api/test_reports.py
      api/test_executor_reports.py mixed_traffic/test_compare_firmware.py
      mixed_traffic/test_compare.py mixed_traffic/test_endpoint_firmware.py codec_plan/test_compare.py rpc_validation/test_compare.py)
    string(REPLACE "/" "_" _name "${_script}")
    string(REPLACE ".py" "" _name "${_name}")
    add_test(NAME benchmark_${_name} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${_script}")
    set_tests_properties(benchmark_${_name} PROPERTIES TIMEOUT 30 LABELS "benchmark;parser")
  endforeach()
endif()

if(NOT WIRELINK_BUILD_BENCHMARKS)
  return()
endif()
if(_wl_wlc)
  add_subdirectory(notification_progress)
endif()

set(RPC_VALIDATION_CODEC_DIR "" CACHE PATH "Frozen codec artifacts for a baseline (no regeneration)")
if(NOT _wl_wlc AND NOT RPC_VALIDATION_CODEC_DIR)
  return()
endif()

set(WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR "" CACHE PATH "Optional Google Benchmark checkout")
if(NOT TARGET benchmark::benchmark AND NOT WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR)
  find_package(benchmark 1.9 CONFIG QUIET)
  if(NOT TARGET benchmark::benchmark)
    message(STATUS "Skipping API and RPC validation benchmarks (Google Benchmark 1.9+ not found)")
    return()
  endif()
endif()

enable_language(CXX)
if(NOT TARGET benchmark::benchmark)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory("${WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR}" google-benchmark EXCLUDE_FROM_ALL)
endif()
if(_wl_wlc)
  add_subdirectory(api)
endif()
add_subdirectory(rpc_validation)
