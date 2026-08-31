# SPDX-License-Identifier: Apache-2.0

find_package(Threads REQUIRED)

add_executable(wirelink_fifo_benchmark fifo_benchmark.c)
target_link_libraries(wirelink_fifo_benchmark PRIVATE
  Wirelink::wirelink
  Threads::Threads
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_fifo_benchmark PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
