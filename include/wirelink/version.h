/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_VERSION_H_
#define WIRELINK_VERSION_H_

#define WIRELINK_VERSION_MAJOR 0
#define WIRELINK_VERSION_MINOR 14
#define WIRELINK_VERSION_PATCH 0
#define WIRELINK_VERSION_STRING "0.14.0"

#define WIRELINK_PROTOCOL_VERSION 1

/*
 * Generated-code contract this core accepts. Required fields have no presence
 * storage in contract 0.14. Older generated descriptors must be regenerated.
 */
#define WIRELINK_CODEGEN_CONTRACT_MAJOR 0
#define WIRELINK_CODEGEN_CONTRACT_MINOR 14

#define WIRELINK_CODEGEN_CONTRACT_ACCEPTS(major, minor) \
    ((major) == WIRELINK_CODEGEN_CONTRACT_MAJOR && \
     (minor) == WIRELINK_CODEGEN_CONTRACT_MINOR)

#endif /* WIRELINK_VERSION_H_ */
