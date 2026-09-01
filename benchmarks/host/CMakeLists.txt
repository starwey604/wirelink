set(WIRELINK_WLC_GENERATED_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../../tests/fixtures/wlc/generated/current")

add_executable(wirelink_transport_benchmark
  transport_benchmark.cpp
  ${WIRELINK_WLC_GENERATED_DIR}/control.c
)
target_compile_features(wirelink_transport_benchmark PRIVATE cxx_std_20)
target_include_directories(wirelink_transport_benchmark PRIVATE
  ${WIRELINK_WLC_GENERATED_DIR}
)
target_link_libraries(wirelink_transport_benchmark PRIVATE
  wirelink::astrial
  wirelink::astrial_usb
  astrial_usb
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_transport_benchmark PRIVATE
    -Wall -Wextra -Wpedantic)
endif()
