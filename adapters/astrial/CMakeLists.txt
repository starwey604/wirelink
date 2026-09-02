add_library(wirelink_astrial src/serial_adapter.cpp)
add_library(wirelink::astrial ALIAS wirelink_astrial)

target_compile_features(wirelink_astrial PUBLIC cxx_std_20)
target_link_libraries(wirelink_astrial PUBLIC wirelink astrial)
target_include_directories(wirelink_astrial PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_astrial PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL AND
   (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE))
  add_executable(wirelink_astrial_virtual_serial_tests
    tests/virtual_serial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/wlc/generated/current/control.c)
  target_include_directories(wirelink_astrial_virtual_serial_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/wlc/generated/current)
  target_link_libraries(wirelink_astrial_virtual_serial_tests
    PRIVATE wirelink_astrial)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(wirelink_astrial_virtual_serial_tests PRIVATE util)
  endif()
  add_test(NAME wirelink_astrial_virtual_serial
    COMMAND wirelink_astrial_virtual_serial_tests)
  set_tests_properties(wirelink_astrial_virtual_serial PROPERTIES TIMEOUT 15)
endif()
