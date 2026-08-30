set(WIRELINK_FUZZ_SANITIZERS
  -fsanitize=fuzzer,address,undefined
  -fno-omit-frame-pointer)

function(wirelink_add_fuzzer target source)
  add_executable(${target} ${source} ${ARGN})
  target_link_libraries(${target} PRIVATE wirelink)
  target_compile_options(${target} PRIVATE ${WIRELINK_FUZZ_SANITIZERS})
  target_link_options(${target} PRIVATE ${WIRELINK_FUZZ_SANITIZERS})
  if(BUILD_TESTING)
    add_test(NAME ${target}_smoke COMMAND ${target} -runs=5000 -max_len=4096
      -dict=${CMAKE_CURRENT_SOURCE_DIR}/wirelink.dict)
    set_tests_properties(${target}_smoke PROPERTIES TIMEOUT 30)
  endif()
endfunction()

wirelink_add_fuzzer(wirelink_fuzz_cobs cobs_decode.c)
wirelink_add_fuzzer(wirelink_fuzz_frame frame_decode.c)
wirelink_add_fuzzer(wirelink_fuzz_protocol protocol_stream.c)
wirelink_add_fuzzer(wirelink_fuzz_generated_codec
  generated_codec.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/wlc/generated/current/control.c)
target_include_directories(wirelink_fuzz_generated_codec PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/wlc/generated/current)
