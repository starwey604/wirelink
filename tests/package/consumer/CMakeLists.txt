cmake_minimum_required(VERSION 3.21)

project(wirelink_package_consumer LANGUAGES C)

include(CTest)
find_package(Wirelink 0.9 CONFIG REQUIRED)

add_executable(wirelink_package_consumer main.c)
target_link_libraries(wirelink_package_consumer PRIVATE Wirelink::wirelink)
target_compile_features(wirelink_package_consumer PRIVATE c_std_11)

add_test(NAME wirelink_package_consumer COMMAND wirelink_package_consumer)
