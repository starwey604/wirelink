# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.21)
project(wirelink_component_rejection LANGUAGES C)
if(WIRELINK_TEST_CPP)
  enable_language(CXX)
endif()
find_package(Wirelink CONFIG REQUIRED COMPONENTS astrial_usb)
