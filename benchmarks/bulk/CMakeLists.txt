# SPDX-License-Identifier: Apache-2.0

add_executable(wirelink_bulk_benchmark bulk_benchmark.c)
target_link_libraries(wirelink_bulk_benchmark PRIVATE Wirelink::wirelink)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_bulk_benchmark PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
