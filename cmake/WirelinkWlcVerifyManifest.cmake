# SPDX-License-Identifier: Apache-2.0
if(NOT DEFINED WIRELINK_WLC_MANIFEST OR NOT DEFINED WIRELINK_WLC_EXPECTED_CONTRACT)
  message(FATAL_ERROR "Wirelink WLC manifest verifier arguments are incomplete")
endif()

file(READ "${WIRELINK_WLC_MANIFEST}" manifest)
string(JSON contract_type TYPE "${manifest}" compiler codegen_contract)
if(NOT contract_type STREQUAL "NUMBER")
  message(FATAL_ERROR "Generated manifest must report an integer codegen contract")
endif()

string(JSON contract GET "${manifest}" compiler codegen_contract)
if(NOT contract STREQUAL WIRELINK_WLC_EXPECTED_CONTRACT)
  message(FATAL_ERROR
    "Generated manifest uses codegen contract ${contract}, expected ${WIRELINK_WLC_EXPECTED_CONTRACT}")
endif()
