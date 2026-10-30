/* SPDX-License-Identifier: Apache-2.0 */

#ifndef INCLUDE_WIRELINK_USB_BULK_DEFAULTS_H_
#define INCLUDE_WIRELINK_USB_BULK_DEFAULTS_H_

#include "wirelink/link_defaults.h"
#include "wirelink/port.h"
#include "wirelink/frame.h"

/* Recommended for directly connected USB Bulk links. Both peers must select
 * the same integrity. Applications may override it before initialization;
 * adapters never infer or replace it when binding an initialized link. */
static inline wl_link_defaults_t wl_usb_bulk_link_defaults(void) {
  wl_link_defaults_t defaults = {WL_INTEGRITY_NONE};
  return defaults;
}

/* USB Bulk always uses native_packet. Select the budget once, apply before
 * link/endpoint initialization, then reuse the same
 * value for explicit RX preparation and adapter binding. No universal native
 * capacity or slot count is prescribed. Both peers must agree on the wire. */
typedef struct {
  wl_integrity_t integrity;
  size_t rx_capacity;
  uint8_t rx_slots; /* 2..WL_RX_UNIT_QUEUE_MAX_SLOTS. */
} wl_usb_bulk_config_t;

typedef enum {
  WL_USB_BULK_RX_FAULT_NONE = 0,
  WL_USB_BULK_RX_FAULT_BOUNDARY_LOST,
  WL_USB_BULK_RX_FAULT_TRANSFER,
  WL_USB_BULK_RX_FAULT_INVALID_COMPLETION,
  WL_USB_BULK_RX_FAULT_QUEUE,
  WL_USB_BULK_RX_FAULT_DISCONNECTED,
} wl_usb_bulk_rx_fault_t;

static inline wl_usb_bulk_config_t wl_usb_bulk_native_packet(
    size_t rx_capacity, uint8_t rx_slots) {
  wl_usb_bulk_config_t value = {
      wl_usb_bulk_link_defaults().integrity,
      rx_capacity, rx_slots};
  return value;
}

static inline wl_err_t wl_usb_bulk_config_validate(
    wl_usb_bulk_config_t config, size_t max_payload_len) {
  if (max_payload_len == 0U || max_payload_len > WL_FRAME_MAX_PAYLOAD ||
      config.rx_capacity == 0U ||
      (config.integrity != WL_INTEGRITY_NONE &&
       config.integrity != WL_INTEGRITY_CRC16 &&
       config.integrity != WL_INTEGRITY_CRC32C)) return WL_ERR_INVALID_ARG;
  if (config.rx_slots < 2U ||
      config.rx_slots > WL_RX_UNIT_QUEUE_MAX_SLOTS ||
      config.rx_capacity > SIZE_MAX / config.rx_slots) return WL_ERR_INVALID_ARG;
  /* Equality leaves a terminating short packet/ZLP for a successor request. */
  return config.rx_capacity > wl_frame_raw_size(max_payload_len, config.integrity)
             ? WL_OK : WL_ERR_BUF_TOO_SMALL;
}

/* Does not touch a live link. Rejection leaves the caller's config intact. */
static inline wl_err_t wl_usb_bulk_config_apply(wl_config_t *link_config,
                                                wl_usb_bulk_config_t config) {
  if (link_config == NULL) return WL_ERR_INVALID_ARG;
  wl_err_t result = wl_usb_bulk_config_validate(config, link_config->max_payload_len);
  if (result != WL_OK) return result;
  link_config->envelope = WL_ENVELOPE_NATIVE_PACKET;
  link_config->integrity = config.integrity;
  return WL_OK;
}

static inline wl_err_t wl_usb_bulk_config_check_link(
    const wl_ctx_t *link, wl_usb_bulk_config_t config) {
  wl_config_t actual;
  wl_err_t result = wl_get_config(link, &actual);
  if (result != WL_OK) return result;
  result = wl_usb_bulk_config_validate(config, actual.max_payload_len);
  if (result != WL_OK) return result;
  return actual.envelope == WL_ENVELOPE_NATIVE_PACKET && actual.integrity == config.integrity
             ? WL_OK : WL_ERR_INVALID_ARG;
}

/* Explicit setup, before binding/starting an adapter and before RX use. The
 * application owns native storage; reinitializing a queue with live leases or
 * claims is forbidden. Adapters never call this or alter link configuration. */
static inline wl_err_t wl_usb_bulk_rx_init(wl_ctx_t *link,
                                           wl_usb_bulk_config_t config,
                                           uint8_t *storage, size_t storage_size) {
  wl_err_t result = wl_usb_bulk_config_check_link(link, config);
  if (result != WL_OK) return result;
  wl_rx_unit_queue_config_t existing;
  if (wl_rx_unit_queue_get_config(link, &existing) == WL_OK)
    return WL_ERR_INVALID_STATE;
  const wl_rx_unit_queue_config_t queue = {
      storage, storage_size, config.rx_capacity, config.rx_slots};
  return wl_rx_unit_queue_init(link, &queue);
}

/* Read-only binding check: even queued data and leased events remain intact. */
static inline wl_err_t wl_usb_bulk_config_check_rx(
    const wl_ctx_t *link, wl_usb_bulk_config_t config) {
  wl_err_t result = wl_usb_bulk_config_check_link(link, config);
  if (result != WL_OK) return result;
  wl_rx_unit_queue_config_t queue;
  result = wl_rx_unit_queue_get_config(link, &queue);
  if (result != WL_OK) return result;
  return queue.unit_size == config.rx_capacity && queue.slot_count == config.rx_slots
             ? WL_OK : WL_ERR_INVALID_ARG;
}

#endif /* INCLUDE_WIRELINK_USB_BULK_DEFAULTS_H_ */
