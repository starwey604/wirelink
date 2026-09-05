cmake_minimum_required(VERSION 3.21)

project(wirelink_package_consumer LANGUAGES C CXX)

include(CTest)
find_package(Wirelink 0.9 CONFIG REQUIRED)

add_executable(wirelink_package_consumer main.c)
target_link_libraries(wirelink_package_consumer PRIVATE Wirelink::wirelink)
target_link_libraries(wirelink_package_consumer PRIVATE Wirelink::loopback)
target_link_libraries(wirelink_package_consumer PRIVATE Wirelink::diagnostics)
set_target_properties(wirelink_package_consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO
  NO_SYSTEM_FROM_IMPORTED YES)

if(MSVC)
  target_compile_options(wirelink_package_consumer PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_package_consumer PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME wirelink_package_consumer COMMAND wirelink_package_consumer)

add_executable(wirelink_package_consumer_cpp20 cpp20_public_headers.cpp)
target_link_libraries(wirelink_package_consumer_cpp20
  PRIVATE Wirelink::wirelink Wirelink::diagnostics)
set_target_properties(wirelink_package_consumer_cpp20 PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  NO_SYSTEM_FROM_IMPORTED YES)

if(MSVC)
  target_compile_options(wirelink_package_consumer_cpp20 PRIVATE
    /W4 /WX /permissive- /Zc:__cplusplus)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_package_consumer_cpp20 PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME wirelink_package_consumer_cpp20
  COMMAND wirelink_package_consumer_cpp20)
