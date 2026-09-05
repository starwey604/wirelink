# SPDX-License-Identifier: Apache-2.0

add_executable(wirelink_rpc_peer_benchmark main.c paths.c)
target_link_libraries(wirelink_rpc_peer_benchmark PRIVATE Wirelink::wirelink)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_rpc_peer_benchmark PRIVATE
    -Wall -Wextra -Wpedantic)
endif()
