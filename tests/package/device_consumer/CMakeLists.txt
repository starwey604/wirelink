# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.21)
project(DeviceConsumer LANGUAGES CXX)
include(CTest)
find_package(DeviceSdk CONFIG REQUIRED)
find_package(CalculatorSdk CONFIG REQUIRED)
add_executable(device_consumer main.cpp)
target_link_libraries(device_consumer PRIVATE DeviceSdk::client CalculatorSdk::client)
add_test(NAME device_consumer COMMAND device_consumer)
