# SPDX-License-Identifier: Apache-2.0
# Pure-Python comparison library: gate/schema/markdown/junit. Its tests are
# part of the PR deterministic layer (compare-parser tests), so this
# directory is added unconditionally, unlike the opt-in benchmark targets.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME benchmark_report_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_report.py")
  set_tests_properties(benchmark_report_tests PROPERTIES
    TIMEOUT 30 LABELS "benchmark;report")
endif()
