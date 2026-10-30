/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_VERSION_H_
#define WIRELINK_VERSION_H_

#define WIRELINK_VERSION_MAJOR 0
#define WIRELINK_VERSION_MINOR 13
#define WIRELINK_VERSION_PATCH 0
#define WIRELINK_VERSION_STRING "0.13.0"

#define WIRELINK_PROTOCOL_VERSION 1

/*
 * Generated-code contract this core accepts. WLC declares the same pair in
 * current generated kernel. Contract 0.12 is retained for frozen standalone
 * public codecs only; its execution headers and engine have been removed. The
 * contract must match exactly before 1.0, and from 1.0 on a generated minor at
 * or below this core's minor is accepted.
 */
#define WIRELINK_CODEGEN_CONTRACT_MAJOR 0
#define WIRELINK_CODEGEN_CONTRACT_MINOR 13

#if WIRELINK_CODEGEN_CONTRACT_MAJOR == 0
#  define WIRELINK_CODEGEN_CONTRACT_ACCEPTS(major, minor) \
     ((major) == WIRELINK_CODEGEN_CONTRACT_MAJOR && \
      ((minor) == WIRELINK_CODEGEN_CONTRACT_MINOR || (minor) == 12))
#else
#  define WIRELINK_CODEGEN_CONTRACT_ACCEPTS(major, minor) \
     ((major) == WIRELINK_CODEGEN_CONTRACT_MAJOR && \
      (minor) <= WIRELINK_CODEGEN_CONTRACT_MINOR)
#endif

#endif /* WIRELINK_VERSION_H_ */
