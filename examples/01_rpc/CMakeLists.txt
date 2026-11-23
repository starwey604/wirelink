# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate(TARGET calculator_server_protocol SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/calculator.wl" NODE server)
wirelink_wlc_generate(TARGET calculator_client_protocol SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/calculator.wl" NODE client)
add_executable(calculator_server server.c)
add_executable(calculator_client client.c client_async.c)
foreach(role server client)
  target_link_libraries(calculator_${role} PRIVATE calculator_${role}_protocol wirelink_tutorial_host)
  wl_warnings(calculator_${role} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endforeach()
