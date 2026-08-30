add_library(wirelink_astrial_usb src/bulk_adapter.cpp)
add_library(wirelink::astrial_usb ALIAS wirelink_astrial_usb)

target_compile_features(wirelink_astrial_usb PUBLIC cxx_std_20)
target_link_libraries(wirelink_astrial_usb PUBLIC wirelink astrial_usb)
target_include_directories(wirelink_astrial_usb PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_astrial_usb PRIVATE -Wall -Wextra -Wpedantic)
endif()
