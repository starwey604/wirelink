# SPDX-License-Identifier: Apache-2.0
add_library(wirelink_tutorial_host STATIC ../common/tutorial_host.cpp)
target_include_directories(wirelink_tutorial_host PUBLIC ../common)
target_link_libraries(wirelink_tutorial_host PUBLIC Wirelink::asio_udp Wirelink::platform)
wl_warnings(wirelink_tutorial_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
foreach(role client server)
  wirelink_wlc_generate(TARGET udp_${role}_protocol SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/udp_demo.wl"
    NODE udp_${role})
  add_executable(udp_peer_${role} ${role}.c)
  target_link_libraries(udp_peer_${role} PRIVATE udp_${role}_protocol wirelink_tutorial_host)
  wl_warnings(udp_peer_${role} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endforeach()
if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME wirelink_udp_peer COMMAND ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/tests/tutorials/udp_peer.py"
    --client $<TARGET_FILE:udp_peer_client> --server $<TARGET_FILE:udp_peer_server>)
  set_tests_properties(wirelink_udp_peer PROPERTIES TIMEOUT 20)
endif()
