# SPDX-License-Identifier: Apache-2.0

add_executable(wirelink_bare_metal_loopback bare_metal_loopback.c)
target_link_libraries(wirelink_bare_metal_loopback PRIVATE Wirelink::wirelink)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_bare_metal_loopback PRIVATE
    -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  add_test(NAME wirelink_bare_metal_loopback
    COMMAND wirelink_bare_metal_loopback)
endif()

if(TARGET wirelink_astrial)
  add_executable(wirelink_astrial_typed_serial
    astrial_typed_serial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/wlc/generated/current/control.c)
  target_include_directories(wirelink_astrial_typed_serial PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/wlc/generated/current)
  target_link_libraries(wirelink_astrial_typed_serial PRIVATE wirelink::astrial)
  target_compile_features(wirelink_astrial_typed_serial PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wirelink_astrial_typed_serial PRIVATE
      -Wall -Wextra -Wpedantic)
  endif()
endif()
