/* SPDX-License-Identifier: Apache-2.0 */

#ifndef INCLUDE_WIRELINK_USB_BULK_DEFAULTS_H_
#define INCLUDE_WIRELINK_USB_BULK_DEFAULTS_H_

#include "wirelink/link_defaults.h"

/* Recommended for directly connected USB Bulk links. Both peers must select
 * the same integrity. Applications may override it before initialization;
 * adapters never infer or replace it when binding an initialized link. */
static inline wl_link_defaults_t wl_usb_bulk_link_defaults(void) {
  wl_link_defaults_t defaults = {WL_INTEGRITY_NONE};
  return defaults;
}

#endif /* INCLUDE_WIRELINK_USB_BULK_DEFAULTS_H_ */
