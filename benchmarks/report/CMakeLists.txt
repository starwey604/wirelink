# SPDX-License-Identifier: Apache-2.0
add_test(NAME benchmark_report_tests
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_report.py")
add_test(NAME benchmark_normalize_tests
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_normalize.py")
add_test(NAME benchmark_current_normalize_tests
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_current_normalize.py")
set_tests_properties(benchmark_report_tests benchmark_normalize_tests benchmark_current_normalize_tests PROPERTIES
  TIMEOUT 30 LABELS "benchmark;report")
