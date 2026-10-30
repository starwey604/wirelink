/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_SRC_LINK_PROFILE_H_
#define WIRELINK_SRC_LINK_PROFILE_H_

#include "wirelink/crc.h"
#include "wirelink/link.h"

/* Set on the core target by both build systems. Direct source builds retain
 * the general library unless they explicitly select the fixed capability. */
#ifndef WL_LINK_PROFILE_COBS_CRC16
#define WL_LINK_PROFILE_COBS_CRC16 0
#endif
#if WL_LINK_PROFILE_COBS_CRC16 != 0 && WL_LINK_PROFILE_COBS_CRC16 != 1
#error "WL_LINK_PROFILE_COBS_CRC16 must be 0 or 1"
#endif

static inline int wl_profile_envelope_valid(wl_envelope_type_t envelope) {
  return envelope >= WL_ENVELOPE_COBS_STREAM &&
         envelope <= WL_ENVELOPE_BUS_LENGTH16;
}

static inline int wl_profile_integrity_valid(wl_integrity_t integrity) {
  return integrity >= WL_INTEGRITY_NONE && integrity <= WL_INTEGRITY_CRC32C;
}

/* Call after selector validity checks. Keep capability selection separate so
 * -Os does not share a larger repeated-validation callee on every frame. */
static inline int wl_profile_supported(wl_envelope_type_t envelope,
                                       wl_integrity_t integrity) {
#if WL_LINK_PROFILE_COBS_CRC16
  return envelope == WL_ENVELOPE_COBS_STREAM && integrity == WL_INTEGRITY_CRC16;
#else
  (void)envelope;
  (void)integrity;
  return 1;
#endif
}

/* These selectors are only for initialized links. Initialization validates
 * the original config; an unsupported request is never silently rewritten. */
static inline wl_envelope_type_t wl_link_envelope(const wl_config_t *config) {
  return config->envelope;
}

static inline wl_integrity_t wl_link_integrity(const wl_config_t *config) {
  return config->integrity;
}

static inline size_t wl_profile_crc_size(wl_integrity_t integrity) {
#if WL_LINK_PROFILE_COBS_CRC16
  (void)integrity;
  return 2U;
#else
  return integrity == WL_INTEGRITY_CRC16 ? 2U :
         integrity == WL_INTEGRITY_CRC32C ? 4U : 0U;
#endif
}

#endif /* WIRELINK_SRC_LINK_PROFILE_H_ */
