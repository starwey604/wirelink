add_library(wirelink_astrial_usb src/bulk_adapter.cpp)
add_library(wirelink::astrial_usb ALIAS wirelink_astrial_usb)

target_compile_features(wirelink_astrial_usb PUBLIC cxx_std_20)
target_link_libraries(wirelink_astrial_usb PUBLIC wirelink_usb_bulk_defaults astrial_usb)
target_include_directories(wirelink_astrial_usb PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_astrial_usb PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
  add_executable(wirelink_astrial_usb_native_tests tests/native.cpp)
  target_link_libraries(wirelink_astrial_usb_native_tests PRIVATE wirelink_astrial_usb)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wirelink_astrial_usb_native_tests PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic -Werror)
  endif()
  foreach(case empty full error capacity queue unprepared mismatch disconnect prequeued leased cobs)
    add_test(NAME wirelink_astrial_usb_native_${case}
      COMMAND wirelink_astrial_usb_native_tests ${case})
    set_tests_properties(wirelink_astrial_usb_native_${case} PROPERTIES TIMEOUT 5)
  endforeach()
endif()
