# SPDX-License-Identifier: Apache-2.0
# Pure-Python benchmark tooling: the shared report library, bench.py and
# presets.toml. Registered unconditionally (relative to the opt-in benchmark
# targets) because its tests need no benchmark build dependencies.
add_subdirectory(report)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME benchmark_bench_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_bench.py")
  set_tests_properties(benchmark_bench_tests PROPERTIES
    TIMEOUT 30 LABELS "benchmark;bench")
endif()

if(Python3_Interpreter_FOUND)
  # Explicit standalone parsers; the real mixed model's argument-dependent
  # tests remain registered by its own CMake target.
  foreach(_script reliable_loss/test_compare.py footprint/test_compare.py
      framing/test_compare.py api/test_kernel_reports.py api/test_reports.py
      api/test_executor_reports.py mixed_traffic/test_compare_firmware.py
      mixed_traffic/test_compare.py mixed_traffic/test_kernel_firmware.py codec_plan/test_compare.py rpc_validation/test_compare.py)
    string(REPLACE "/" "_" _name "${_script}")
    string(REPLACE ".py" "" _name "${_name}")
    add_test(NAME benchmark_${_name} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${_script}")
    set_tests_properties(benchmark_${_name} PROPERTIES TIMEOUT 30 LABELS "benchmark;parser")
  endforeach()
endif()
