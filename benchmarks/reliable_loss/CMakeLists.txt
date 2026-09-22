# SPDX-License-Identifier: Apache-2.0

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(wirelink_reliable_loss reliable_loss.c)
target_link_libraries(wirelink_reliable_loss PRIVATE Wirelink::wirelink)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_reliable_loss PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
  add_test(NAME reliable_loss_smoke
    COMMAND wirelink_reliable_loss --samples 200 --loss 0,10,50
      --json "${CMAKE_CURRENT_BINARY_DIR}/reliable_loss.json")
  set_tests_properties(reliable_loss_smoke PROPERTIES
    LABELS "benchmark;smoke" TIMEOUT 60)
  add_test(NAME reliable_loss_reports
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_compare.py")
  set_tests_properties(reliable_loss_reports PROPERTIES LABELS "benchmark;smoke")
endif()
