add_library(wirelink_asio_udp src/udp_adapter.cpp)
add_library(wirelink::asio_udp ALIAS wirelink_asio_udp)

if(WIRELINK_ASIO_INCLUDE_DIR STREQUAL "")
  find_path(WIRELINK_ASIO_INCLUDE_DIR asio.hpp)
endif()
if(NOT WIRELINK_ASIO_INCLUDE_DIR)
  message(FATAL_ERROR
    "WIRELINK_BUILD_ASIO_UDP_ADAPTER requires WIRELINK_ASIO_INCLUDE_DIR")
endif()

find_package(Threads REQUIRED)
target_compile_features(wirelink_asio_udp PUBLIC cxx_std_20)
target_compile_definitions(wirelink_asio_udp PRIVATE ASIO_STANDALONE)
target_include_directories(wirelink_asio_udp
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE ${WIRELINK_ASIO_INCLUDE_DIR})
target_link_libraries(wirelink_asio_udp PUBLIC wirelink PRIVATE Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_asio_udp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  add_executable(wirelink_asio_udp_loopback_tests tests/loopback.cpp)
  target_link_libraries(wirelink_asio_udp_loopback_tests PRIVATE wirelink_asio_udp)
  add_test(NAME wirelink_asio_udp_loopback
    COMMAND wirelink_asio_udp_loopback_tests)
  set_tests_properties(wirelink_asio_udp_loopback PROPERTIES TIMEOUT 10)
endif()
