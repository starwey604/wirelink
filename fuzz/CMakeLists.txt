# SPDX-License-Identifier: Apache-2.0
set(WIRELINK_FUZZ_SANITIZERS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
# Same production implementation, compiled with coverage/sanitizers for fuzzing.
# No alternate execution semantics or installed library is introduced.
add_library(wirelink_fuzz_core STATIC
  ../src/kernel.c ../src/packet_port.c ../src/native_none.c ../src/rpc_wire.c
  ../src/packet_stream.c ../src/cobs_crc16_stream.c ../src/length16_none_stream.c
  ../src/cobs.c ../src/crc.c ../src/frame.c ../src/frame_cobs_crc16.c ../src/types.c)
target_include_directories(wirelink_fuzz_core PUBLIC ../include)
target_compile_options(wirelink_fuzz_core PRIVATE -fsanitize=fuzzer-no-link,address,undefined
  -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Werror)
function(wirelink_add_fuzzer target source)
  add_executable(${target} ${source} ${ARGN})
  target_link_libraries(${target} PRIVATE wirelink_fuzz_core)
  target_compile_options(${target} PRIVATE ${WIRELINK_FUZZ_SANITIZERS} -Wall -Wextra -Wpedantic -Werror)
  target_link_options(${target} PRIVATE ${WIRELINK_FUZZ_SANITIZERS})
  if(BUILD_TESTING)
    add_test(NAME ${target}_smoke COMMAND ${target} -runs=5000 -max_len=4096
      -dict=${CMAKE_CURRENT_SOURCE_DIR}/wirelink.dict)
    set_tests_properties(${target}_smoke PROPERTIES TIMEOUT 30)
  endif()
endfunction()
wirelink_add_fuzzer(wirelink_fuzz_cobs cobs_decode.c)
wirelink_add_fuzzer(wirelink_fuzz_frame frame_decode.c)
wirelink_add_fuzzer(wirelink_fuzz_frame_encode frame_encode.c)
wirelink_add_fuzzer(wirelink_fuzz_kernel kernel.c)
wirelink_add_fuzzer(wirelink_fuzz_packet_stream packet_stream.c)
# Fuzz the current codec without rewriting frozen 0.12 conformance bytes.
wirelink_wlc_generate_codec(TARGET wirelink_fuzz_codec
  SCHEMA "${PROJECT_SOURCE_DIR}/tests/fixtures/wlc/current/control.wl")
target_compile_options(wirelink_fuzz_codec PRIVATE -fsanitize=fuzzer-no-link,address,undefined
  -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Werror)
wirelink_add_fuzzer(wirelink_fuzz_generated_codec generated_codec.c)
target_link_libraries(wirelink_fuzz_generated_codec PRIVATE wirelink_fuzz_codec)
