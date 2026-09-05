# SPDX-License-Identifier: Apache-2.0

add_library(wirelink_diagnostics src/diagnostics.c)
add_library(Wirelink::diagnostics ALIAS wirelink_diagnostics)
target_include_directories(wirelink_diagnostics PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
set_target_properties(wirelink_diagnostics PROPERTIES EXPORT_NAME diagnostics)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_diagnostics PRIVATE
    -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING AND wirelink_IS_TOP_LEVEL)
  add_executable(wirelink_diagnostics_test tests/diagnostics.c)
  target_link_libraries(wirelink_diagnostics_test PRIVATE
    Wirelink::diagnostics)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wirelink_diagnostics_test PRIVATE
      -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME wirelink_diagnostics COMMAND wirelink_diagnostics_test)
endif()
