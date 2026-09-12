# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.21)
project(calculator_package_consumer LANGUAGES CXX)
include(CTest)
find_package(CalculatorSdk 0.1 CONFIG REQUIRED)
add_executable(calculator_consumer main.cpp)
target_link_libraries(calculator_consumer PRIVATE CalculatorSdk::client)
add_test(NAME calculator_installed_consumer COMMAND calculator_consumer)
