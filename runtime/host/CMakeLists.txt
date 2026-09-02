find_package(Threads REQUIRED)

add_library(wirelink_host
  src/executor.cpp
)
add_library(Wirelink::host ALIAS wirelink_host)

target_compile_features(wirelink_host PUBLIC cxx_std_20)
target_include_directories(wirelink_host PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(wirelink_host PUBLIC
  Wirelink::wirelink
  Threads::Threads
)
set_target_properties(wirelink_host PROPERTIES EXPORT_NAME host)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
  add_subdirectory(tests)
endif()
