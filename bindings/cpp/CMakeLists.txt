# SPDX-License-Identifier: Apache-2.0
add_library(wirelink_cpp INTERFACE)
add_library(Wirelink::cpp ALIAS wirelink_cpp)
set_target_properties(wirelink_cpp PROPERTIES EXPORT_NAME cpp)
target_compile_features(wirelink_cpp INTERFACE cxx_std_20)
target_include_directories(wirelink_cpp INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(wirelink_cpp INTERFACE wirelink_kernel wirelink_host)
add_library(wirelink_cpp_host STATIC src/session.cpp)
add_library(Wirelink::cpp_host ALIAS wirelink_cpp_host)
set_target_properties(wirelink_cpp_host PROPERTIES EXPORT_NAME cpp_host POSITION_INDEPENDENT_CODE ON)
target_link_libraries(wirelink_cpp_host PUBLIC wirelink_cpp wirelink_platform PRIVATE wirelink_asio_udp)
wl_warnings(wirelink_cpp_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(WIRELINK_INSTALL)
  install(TARGETS wirelink_cpp wirelink_cpp_host EXPORT Wirelink_cpp_hostTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(DIRECTORY include/wirelink/cpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink)
  install(EXPORT Wirelink_cpp_hostTargets NAMESPACE Wirelink:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Wirelink)
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
