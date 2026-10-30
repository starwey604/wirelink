# SPDX-License-Identifier: Apache-2.0
find_package(Threads REQUIRED)
add_library(wirelink_host STATIC src/executor.cpp)
add_library(Wirelink::host ALIAS wirelink_host)
set_target_properties(wirelink_host PROPERTIES EXPORT_NAME host POSITION_INDEPENDENT_CODE ON)
target_compile_features(wirelink_host PUBLIC cxx_std_20)
target_include_directories(wirelink_host PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(wirelink_host PUBLIC wirelink_kernel Threads::Threads)
wl_warnings(wirelink_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(WIRELINK_INSTALL)
  install(TARGETS wirelink_host EXPORT Wirelink_hostTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(DIRECTORY include/wirelink/host DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink)
  install(EXPORT Wirelink_hostTargets NAMESPACE Wirelink:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Wirelink)
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
