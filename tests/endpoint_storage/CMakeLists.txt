# SPDX-License-Identifier: Apache-2.0
add_library(wirelink_storage_bridge SHARED bridge.c)
target_compile_definitions(wirelink_storage_bridge PRIVATE STORAGE_BRIDGE_BUILD)
target_link_libraries(wirelink_storage_bridge PRIVATE calculator_protocol Wirelink::storage Wirelink::packet_loopback)
add_executable(wirelink_storage_bridge_cpp consumer.cpp)
target_compile_features(wirelink_storage_bridge_cpp PRIVATE cxx_std_20)
target_link_libraries(wirelink_storage_bridge_cpp PRIVATE wirelink_storage_bridge)
add_test(NAME wirelink_storage_bridge_cpp COMMAND wirelink_storage_bridge_cpp)
add_test(NAME wirelink_storage_bridge_python COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/consumer.py $<TARGET_FILE:wirelink_storage_bridge>)
set_tests_properties(wirelink_storage_bridge_cpp wirelink_storage_bridge_python PROPERTIES TIMEOUT 30)
wl_warnings(wirelink_storage_bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)
wl_warnings(wirelink_storage_bridge_cpp PRIVATE -Wall -Wextra -Wpedantic -Werror)
