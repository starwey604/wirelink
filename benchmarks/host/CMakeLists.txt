# SPDX-License-Identifier: Apache-2.0
add_executable(wirelink_transport_benchmark transport_benchmark.cpp)
target_compile_features(wirelink_transport_benchmark PRIVATE cxx_std_20)
target_include_directories(wirelink_transport_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/examples/common)
target_link_libraries(wirelink_transport_benchmark PRIVATE wirelink_packet_tool_io wirelink_packet_tool_control Wirelink::framing_primitives)
wl_warnings(wirelink_transport_benchmark PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BUILD_TESTING)
  add_executable(wirelink_packet_object_test
    ${PROJECT_SOURCE_DIR}/tests/packet_tools/object_client.cpp
    ${PROJECT_SOURCE_DIR}/tests/packet_tools/object_peer.c
    ${PROJECT_SOURCE_DIR}/samples/zephyr/usb_bulk/src/application.c)
  target_include_directories(wirelink_packet_object_test PRIVATE ${PROJECT_SOURCE_DIR}/samples/zephyr/usb_bulk/src ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(wirelink_packet_object_test PRIVATE wirelink_packet_tool_io wirelink_packet_tool_control Wirelink::framing_primitives)
  wl_warnings(wirelink_packet_object_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME packet_object_client COMMAND wirelink_packet_object_test)
  set_tests_properties(packet_object_client PROPERTIES TIMEOUT 10)
endif()
