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
