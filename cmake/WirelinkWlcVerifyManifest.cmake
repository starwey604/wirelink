if(NOT DEFINED WIRELINK_WLC_MANIFEST OR
    NOT DEFINED WIRELINK_WLC_EXPECTED_VERSION OR
    NOT DEFINED WIRELINK_WLC_EXPECTED_ABI)
  message(FATAL_ERROR "Wirelink WLC manifest verifier arguments are incomplete")
endif()

if(NOT EXISTS "${WIRELINK_WLC_MANIFEST}")
  message(FATAL_ERROR
    "WLC did not produce the expected manifest: ${WIRELINK_WLC_MANIFEST}")
endif()

file(READ "${WIRELINK_WLC_MANIFEST}" _manifest)
string(JSON _compiler_name GET "${_manifest}" compiler name)
string(JSON _compiler_version GET "${_manifest}" compiler version)
string(JSON _codegen_abi GET "${_manifest}" compiler codegen_abi)

if(NOT _compiler_name STREQUAL "wlc")
  message(FATAL_ERROR
    "Generated manifest names compiler '${_compiler_name}', expected 'wlc'")
endif()
if(NOT _compiler_version STREQUAL WIRELINK_WLC_EXPECTED_VERSION)
  message(FATAL_ERROR
    "Generated manifest uses WLC ${_compiler_version}, expected "
    "${WIRELINK_WLC_EXPECTED_VERSION}")
endif()
if(NOT _codegen_abi STREQUAL WIRELINK_WLC_EXPECTED_ABI)
  message(FATAL_ERROR
    "Generated manifest uses WLC codegen ABI ${_codegen_abi}, expected "
    "${WIRELINK_WLC_EXPECTED_ABI}")
endif()
