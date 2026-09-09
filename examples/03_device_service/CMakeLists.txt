# SPDX-License-Identifier: Apache-2.0
wirelink_wlc_generate_codec(TARGET device_codec SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/device.wl")
foreach(role client server)
  wirelink_wlc_generate_runtime(TARGET device_${role}_protocol CODEC_TARGET device_codec
    RUNTIME_NAME device_${role}
    PROFILES "${CMAKE_CURRENT_SOURCE_DIR}/services.bind.wl" "${CMAKE_CURRENT_SOURCE_DIR}/${role}.bind.wl")
  add_executable(device_service_${role} ${role}.c)
  target_link_libraries(device_service_${role} PRIVATE device_${role}_protocol wirelink_tutorial_host)
endforeach()
target_sources(device_service_server PRIVATE device_service.c calculator_service.c self_test.c)

if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME wirelink_device_service
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/tutorials/device_service.py
      --client $<TARGET_FILE:device_service_client> --server $<TARGET_FILE:device_service_server>)
  set_tests_properties(wirelink_device_service PROPERTIES TIMEOUT 30)
  get_target_property(_wlc device_codec WIRELINK_WLC_EXECUTABLE)
  add_test(NAME wirelink_device_service_extension
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/tutorials/device_service_extension.py
      --source ${PROJECT_SOURCE_DIR} --wlc "${_wlc}" --asio "${WIRELINK_ASIO_INCLUDE_DIR}"
      --cmake "${CMAKE_COMMAND}" --generator "${CMAKE_GENERATOR}"
      --c-compiler "${CMAKE_C_COMPILER}" --cxx-compiler "${CMAKE_CXX_COMPILER}")
  set_tests_properties(wirelink_device_service_extension PROPERTIES TIMEOUT 210)
endif()
