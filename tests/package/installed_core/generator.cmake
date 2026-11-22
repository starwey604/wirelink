# SPDX-License-Identifier: Apache-2.0
get_filename_component(fixture "${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures/static_endpoint" ABSOLUTE)
file(READ "${fixture}/probe.wl" schema)
string(REPLACE "handlers = dynamic;" "handlers = dynamic; framing = cobs_crc16;" schema "${schema}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cobs-schema")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cobs-schema/probe.wl" "${schema}")
wirelink_wlc_generate(TARGET probe_native SCHEMA "${fixture}/probe.wl")
wirelink_wlc_generate(TARGET probe_cobs SCHEMA "${CMAKE_CURRENT_BINARY_DIR}/cobs-schema/probe.wl")
consumer(installed_c native.c probe_native Wirelink::packet_loopback)
consumer(installed_cpp generated.cpp probe_native)
consumer(installed_cobs cobs.c probe_cobs)
if(WIRELINK_CONSUMER_HOST)
  consumer(installed_session "${CMAKE_CURRENT_SOURCE_DIR}/../../../bindings/cpp/tests/session.cpp"
    probe_native Wirelink::cpp_host Wirelink::udp)
  set_tests_properties(installed_session PROPERTIES TIMEOUT 30)
endif()
# Keyword escaping is shared with WLC; no CMake-side copy of identifier rules.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/switch.wl" "version 1; message Empty @id(1) {}\n")
wirelink_wlc_generate_codec(TARGET keyword_codec SCHEMA "${CMAKE_CURRENT_BINARY_DIR}/switch.wl")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/keyword.c"
  "#include \"switch_.h\"\nint main(void) { empty_t value; empty_clear(&value); return 0; }\n")
consumer(installed_keyword "${CMAKE_CURRENT_BINARY_DIR}/keyword.c" keyword_codec)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME generator_diagnostics COMMAND ${Python3_EXECUTABLE}
  "${CMAKE_CURRENT_SOURCE_DIR}/check_diagnostics.py" --cmake "${CMAKE_COMMAND}"
  --prefix "${CMAKE_PREFIX_PATH}" --wlc "${WIRELINK_WLC_EXECUTABLE}")
# Compile/link with the consumer defaults: no function-section/GC requirement.
wirelink_wlc_generate(TARGET roles_client SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/roles.wl" NODE client)
wirelink_wlc_generate(TARGET roles_all SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/roles.wl" ALL_NODES)
consumer(installed_client_only client_only.c roles_client)
consumer(installed_all_nodes all_nodes.c roles_all)
