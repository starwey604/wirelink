if(NOT DEFINED WIRELINK_WLC_MANIFEST OR
    NOT DEFINED WIRELINK_WLC_EXPECTED_VERSION OR
    NOT DEFINED WIRELINK_WLC_EXPECTED_CONTRACT_MAJOR OR
    NOT DEFINED WIRELINK_WLC_EXPECTED_CONTRACT_MINOR)
  message(FATAL_ERROR "Wirelink WLC manifest verifier arguments are incomplete")
endif()

if(NOT EXISTS "${WIRELINK_WLC_MANIFEST}")
  message(FATAL_ERROR
    "WLC did not produce the expected manifest: ${WIRELINK_WLC_MANIFEST}")
endif()

file(READ "${WIRELINK_WLC_MANIFEST}" _manifest)
string(JSON _compiler_name GET "${_manifest}" compiler name)
string(JSON _compiler_version GET "${_manifest}" compiler version)
string(JSON _contract_major GET "${_manifest}" compiler codegen_contract major)
string(JSON _contract_minor GET "${_manifest}" compiler codegen_contract minor)

if(NOT _compiler_name STREQUAL "wlc")
  message(FATAL_ERROR
    "Generated manifest names compiler '${_compiler_name}', expected 'wlc'")
endif()
if(NOT _compiler_version STREQUAL WIRELINK_WLC_EXPECTED_VERSION)
  message(FATAL_ERROR
    "Generated manifest uses WLC ${_compiler_version}, expected "
    "${WIRELINK_WLC_EXPECTED_VERSION}")
endif()
if(NOT _contract_major STREQUAL WIRELINK_WLC_EXPECTED_CONTRACT_MAJOR)
  message(FATAL_ERROR
    "Generated manifest uses codegen contract ${_contract_major}.${_contract_minor}, "
    "expected major ${WIRELINK_WLC_EXPECTED_CONTRACT_MAJOR}")
endif()
if(WIRELINK_WLC_EXPECTED_CONTRACT_MAJOR STREQUAL "0")
  # Before 1.0 the contract must match exactly.
  if(NOT _contract_minor STREQUAL WIRELINK_WLC_EXPECTED_CONTRACT_MINOR)
    message(FATAL_ERROR
      "Generated manifest uses codegen contract ${_contract_major}.${_contract_minor}, "
      "expected ${WIRELINK_WLC_EXPECTED_CONTRACT_MAJOR}.${WIRELINK_WLC_EXPECTED_CONTRACT_MINOR}")
  endif()
elseif(_contract_minor GREATER WIRELINK_WLC_EXPECTED_CONTRACT_MINOR)
  message(FATAL_ERROR
    "Generated manifest uses codegen contract ${_contract_major}.${_contract_minor}, "
    "newer than the supported ${WIRELINK_WLC_EXPECTED_CONTRACT_MAJOR}.${WIRELINK_WLC_EXPECTED_CONTRACT_MINOR}")
endif()
