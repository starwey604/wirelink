# SPDX-License-Identifier: Apache-2.0
add_executable(wirelink_session_tests)
set(SESSION_TEST_TARGET wirelink_session_tests)
include(matrix.cmake)
target_link_libraries(wirelink_session_tests PRIVATE Wirelink::platform)
add_test(NAME wirelink_session_matrix COMMAND wirelink_session_tests)
