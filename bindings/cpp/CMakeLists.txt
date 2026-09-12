# SPDX-License-Identifier: Apache-2.0
add_library(wirelink_cpp INTERFACE)
add_library(Wirelink::cpp ALIAS wirelink_cpp)
set_target_properties(wirelink_cpp PROPERTIES EXPORT_NAME cpp)
target_compile_features(wirelink_cpp INTERFACE cxx_std_20)
target_include_directories(wirelink_cpp INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(wirelink_cpp INTERFACE Wirelink::wirelink)

add_library(wirelink_cpp_host STATIC src/session.cpp)
add_library(Wirelink::cpp_host ALIAS wirelink_cpp_host)
set_target_properties(wirelink_cpp_host PROPERTIES EXPORT_NAME cpp_host POSITION_INDEPENDENT_CODE ON)
target_link_libraries(wirelink_cpp_host PUBLIC Wirelink::cpp
  PRIVATE Wirelink::host Wirelink::asio_udp)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_cpp_host PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIRELINK_INSTALL)
  install(TARGETS wirelink_cpp wirelink_cpp_host EXPORT WirelinkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(DIRECTORY include/wirelink/cpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink)
endif()
