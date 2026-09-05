# SPDX-License-Identifier: Apache-2.0

add_library(wirelink_loopback src/loopback.c)
add_library(Wirelink::loopback ALIAS wirelink_loopback)
target_link_libraries(wirelink_loopback PUBLIC Wirelink::wirelink)
set_target_properties(wirelink_loopback PROPERTIES EXPORT_NAME loopback)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_loopback PRIVATE
    -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  add_executable(wirelink_loopback_test tests/loopback.c)
  target_link_libraries(wirelink_loopback_test PRIVATE Wirelink::loopback)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wirelink_loopback_test PRIVATE
      -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME wirelink_loopback COMMAND wirelink_loopback_test)
endif()
