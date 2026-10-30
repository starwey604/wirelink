/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_FRAME_PROFILE_H_
#define WIRELINK_FRAME_PROFILE_H_
#include "wirelink/frame.h"
/* Selectors belong to the standalone all-format tool, never an endpoint. */
static inline int wl_profile_envelope_valid(wl_envelope_type_t e) {
  return e >= WL_ENVELOPE_COBS_STREAM && e <= WL_ENVELOPE_BUS_LENGTH16;
}
static inline int wl_profile_integrity_valid(wl_integrity_t i) {
  return i >= WL_INTEGRITY_NONE && i <= WL_INTEGRITY_CRC32C;
}
static inline int wl_profile_supported(wl_envelope_type_t e, wl_integrity_t i) {
  (void)e; (void)i; return 1;
}
static inline size_t wl_profile_crc_size(wl_integrity_t i) {
  return i == WL_INTEGRITY_CRC16 ? 2U : i == WL_INTEGRITY_CRC32C ? 4U : 0U;
}
#endif
