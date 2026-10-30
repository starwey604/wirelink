/* SPDX-License-Identifier: Apache-2.0 */

#ifndef INCLUDE_WIRELINK_LINK_DEFAULTS_H_
#define INCLUDE_WIRELINK_LINK_DEFAULTS_H_

#include "wirelink/link.h"

/* Setup-only value descriptor. Apply defaults before application overrides;
 * neither this value nor its address is retained by the link or endpoint. */
typedef struct {
  wl_integrity_t integrity;
} wl_link_defaults_t;

static inline wl_link_defaults_t wl_link_defaults(void) {
  wl_link_defaults_t defaults = {WL_INTEGRITY_CRC16};
  return defaults;
}

/* Only the default fields are written. Envelope, session, payload limits and
 * all other caller configuration remain intact. Capability checks belong to
 * wl_init(), so a valid but unavailable integrity is never downgraded. */
static inline wl_err_t wl_config_apply_link_defaults(
    wl_config_t *config, wl_link_defaults_t defaults) {
  if (config == NULL || (defaults.integrity != WL_INTEGRITY_NONE &&
      defaults.integrity != WL_INTEGRITY_CRC16 &&
      defaults.integrity != WL_INTEGRITY_CRC32C)) return WL_ERR_INVALID_ARG;
  config->integrity = defaults.integrity;
  return WL_OK;
}

#endif /* INCLUDE_WIRELINK_LINK_DEFAULTS_H_ */
