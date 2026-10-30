# SPDX-License-Identifier: Apache-2.0
add_executable(packet_tool_owner_test main.c ../../examples/common/packet_tool_owner.c)
target_include_directories(packet_tool_owner_test PRIVATE ../../examples/common)
target_link_libraries(packet_tool_owner_test PRIVATE Wirelink::kernel Wirelink::byte_fifo_cobs)
wl_warnings(packet_tool_owner_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME packet_tool_owner COMMAND packet_tool_owner_test)
