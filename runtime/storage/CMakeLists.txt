# SPDX-License-Identifier: Apache-2.0
add_library(wirelink_storage src/fixed_pool.c)
add_library(Wirelink::storage ALIAS wirelink_storage)
set_target_properties(wirelink_storage PROPERTIES EXPORT_NAME storage)
target_link_libraries(wirelink_storage PUBLIC wirelink)
target_include_directories(wirelink_storage PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_storage PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIRELINK_INSTALL)
  install(TARGETS wirelink_storage EXPORT WirelinkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(DIRECTORY include/wirelink/storage DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wirelink)
endif()
