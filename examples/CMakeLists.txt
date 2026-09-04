# SPDX-License-Identifier: Apache-2.0

add_executable(wirelink_bare_metal_loopback bare_metal_loopback.c)
target_link_libraries(wirelink_bare_metal_loopback PRIVATE Wirelink::wirelink)

if(WIRELINK_BUILD_GETTING_STARTED)
  wirelink_wlc_generate(
    TARGET wirelink_quickstart_protocol
    SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/getting_started/quickstart.wl"
    PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/getting_started/quickstart.bind.wl")
  add_executable(wirelink_getting_started getting_started.c)
  target_link_libraries(wirelink_getting_started PRIVATE
    wirelink_quickstart_protocol)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_bare_metal_loopback PRIVATE
    -Wall -Wextra -Wpedantic)
  if(TARGET wirelink_getting_started)
    target_compile_options(wirelink_getting_started PRIVATE
      -Wall -Wextra -Wpedantic)
  endif()
endif()

if(BUILD_TESTING)
  add_test(NAME wirelink_bare_metal_loopback
    COMMAND wirelink_bare_metal_loopback)
  if(TARGET wirelink_getting_started)
    add_test(NAME wirelink_getting_started
      COMMAND wirelink_getting_started)
  endif()
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
