# SPDX-License-Identifier: Apache-2.0
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR "" CACHE PATH "Optional Google Benchmark source checkout")
if(WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory("${WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/google-benchmark" EXCLUDE_FROM_ALL)
else()
  find_package(benchmark 1.9 CONFIG REQUIRED)
endif()
if(NOT TARGET Wirelink::platform)
  message(FATAL_ERROR "API benchmarks require WIRELINK_BUILD_PLATFORM=ON")
endif()
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  message(WARNING "Use a Release build for performance comparisons; other builds are functional checks only")
endif()

function(api_variant services slots delivery)
  set(name "s${services}_c${slots}_${delivery}")
  set(schema_dir "${CMAKE_CURRENT_BINARY_DIR}/${name}/schema")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/make_schema.py"
    --services ${services} --delivery ${delivery} --output "${schema_dir}"
    COMMAND_ERROR_IS_FATAL ANY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/make_schema.py")
  wirelink_wlc_generate_codec(TARGET api_codec_${name} SCHEMA "${schema_dir}/load.wl")
  wirelink_wlc_generate_runtime(TARGET api_runtime_${name} CODEC_TARGET api_codec_${name}
    PROFILE "${schema_dir}/load.bind.wl" RUNTIME_NAME perf)
  target_compile_definitions(api_runtime_${name} PUBLIC
    PERF_ENDPOINT_RPC_CAPACITY=${slots} API_SERVICES=${services} API_VARIANT="${name}")
  add_executable(wirelink_api_${name} micro.cpp)
  target_link_libraries(wirelink_api_${name} PRIVATE api_runtime_${name}
    benchmark::benchmark Wirelink::loopback)
  target_compile_features(wirelink_api_${name} PRIVATE cxx_std_20)
  target_compile_definitions(wirelink_api_${name} PRIVATE
    API_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
    API_BUILD_TYPE="$<CONFIG>")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wirelink_api_${name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  if(BUILD_TESTING)
    add_test(NAME api_benchmark_${name} COMMAND wirelink_api_${name}
      --benchmark_min_time=0.001s --benchmark_repetitions=1)
    set_tests_properties(api_benchmark_${name} PROPERTIES TIMEOUT 45 LABELS "benchmark;smoke")
  endif()
endfunction()

api_variant(1 4 reliable)
api_variant(12 1 reliable)
api_variant(12 4 reliable)
api_variant(64 4 reliable)
api_variant(12 4 unreliable)

if(WIRELINK_BUILD_EXECUTOR_BENCHMARK)
  api_variant(12 8 reliable)
  add_executable(wirelink_executor_benchmark executor.cpp)
  target_link_libraries(wirelink_executor_benchmark PRIVATE api_runtime_s12_c8_reliable
    Wirelink::loopback Wirelink::host)
  target_compile_features(wirelink_executor_benchmark PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wirelink_executor_benchmark PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  if(BUILD_TESTING)
    add_test(NAME executor_benchmark_reports COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/test_executor_reports.py")
    set_tests_properties(executor_benchmark_reports PROPERTIES LABELS "benchmark;smoke")
    foreach(mode rpc proxy latest shared)
      add_test(NAME executor_benchmark_${mode} COMMAND wirelink_executor_benchmark ${mode} 4 100 32 0)
      set_tests_properties(executor_benchmark_${mode} PROPERTIES TIMEOUT 30 LABELS "benchmark;concurrency")
    endforeach()
  endif()
endif()

add_library(api_udp_support STATIC ../../examples/common/tutorial_host.cpp)
target_include_directories(api_udp_support PUBLIC ../../examples/common)
target_link_libraries(api_udp_support PUBLIC Wirelink::asio_udp Wirelink::platform)
add_executable(wirelink_api_udp udp.cpp)
target_link_libraries(wirelink_api_udp PRIVATE api_runtime_s12_c4_reliable api_udp_support)
target_compile_features(wirelink_api_udp PRIVATE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wirelink_api_udp PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(BUILD_TESTING)
  add_test(NAME api_benchmark_reports COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_reports.py")
  add_test(NAME api_benchmark_udp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
    --bin-dir "$<TARGET_FILE_DIR:wirelink_api_udp>" --skip-micro --samples 100 --warmup 10 --udp-rounds 1)
  # Correctness only: no timing threshold and no persistent baseline to overwrite.
  set_tests_properties(api_benchmark_reports PROPERTIES LABELS "benchmark;smoke")
  set_tests_properties(api_benchmark_udp PROPERTIES LABELS "benchmark;smoke" TIMEOUT 45)
endif()
