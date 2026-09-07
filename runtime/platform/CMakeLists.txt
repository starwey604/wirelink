# SPDX-License-Identifier: Apache-2.0
add_library(wirelink_platform native.c)
add_library(Wirelink::platform ALIAS wirelink_platform)
set_target_properties(wirelink_platform PROPERTIES EXPORT_NAME platform)
target_link_libraries(wirelink_platform PUBLIC Wirelink::wirelink)
if(MSVC)
  target_compile_options(wirelink_platform PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_platform PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
  target_link_libraries(wirelink_platform PRIVATE bcrypt)
elseif(APPLE)
  target_link_libraries(wirelink_platform PRIVATE "-framework Security")
endif()
if(WIRELINK_INSTALL)
  install(TARGETS wirelink_platform EXPORT WirelinkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
