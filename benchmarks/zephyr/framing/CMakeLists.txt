# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirelink_framing_cpu LANGUAGES C)
target_sources(app PRIVATE src/main.c ../../framing/workload.c)
target_include_directories(app PRIVATE ../../framing)
