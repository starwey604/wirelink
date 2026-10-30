/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_ZEPHYR_USB_BULK_H_
#define WIRELINK_ZEPHYR_USB_BULK_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/atomic.h>

#include "wirelink/port.h"
#include "wirelink/usb_bulk_defaults.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initial descriptor addresses only. Zephyr may reassign them in usbd_init(). */
#define WL_ZEPHYR_USB_BULK_OUT_EP 0x01U
#define WL_ZEPHYR_USB_BULK_IN_EP 0x81U
#define WL_ZEPHYR_USB_BULK_INTERFACE_SUBCLASS 0x57U
#define WL_ZEPHYR_USB_BULK_INTERFACE_PROTOCOL 0x4CU

typedef uint32_t (*wl_zephyr_usb_bulk_cycle_count_fn)(void *user_data);
typedef void (*wl_zephyr_usb_bulk_wake_fn)(void *user_data);

typedef struct wl_zephyr_usb_bulk_config {
  wl_ctx_t *link;
  /* Same selection applied before link init. Prepare caller-owned native
   * storage explicitly with wl_usb_bulk_rx_init() before binding. */
  wl_usb_bulk_config_t transport;
  /* Optional ISR-safe notification for the single Wirelink consumer. */
  void *wake_user_data;
  wl_zephyr_usb_bulk_wake_fn wake_consumer;
  /* Optional thread/ISR-safe cycle counter used only for instrumentation. */
  void *cycle_counter_user_data;
  wl_zephyr_usb_bulk_cycle_count_fn cycle_counter;
} wl_zephyr_usb_bulk_config_t;

typedef struct wl_zephyr_usb_bulk_stats {
  uint32_t rx_claims;
  uint32_t rx_completions;
  uint32_t rx_bytes;
  uint32_t rx_pauses;
  uint32_t tx_submissions;
  uint32_t tx_completions;
  uint32_t tx_bytes;
  uint32_t rx_callback_cycles;
  uint32_t rx_callback_max_cycles;
  uint32_t tx_callback_cycles;
  uint32_t tx_callback_max_cycles;
  uint32_t tx_sink_cycles;
  uint32_t tx_sink_max_cycles;
  uint32_t active_service_calls;
  uint32_t active_service_cycles;
  uint32_t active_service_max_cycles;
  uint32_t errors;
  bool enabled;
  bool rx_active;
  bool tx_active;
  wl_usb_bulk_rx_fault_t rx_fault;
  wl_usb_bulk_rx_fault_t last_rx_fault;
  bool recovering;
} wl_zephyr_usb_bulk_stats_t;

typedef struct wl_zephyr_usb_bulk {
  wl_ctx_t *link;
  size_t maximum_rx_size;
  void *wake_user_data;
  wl_zephyr_usb_bulk_wake_fn wake_consumer;
  void *cycle_counter_user_data;
  wl_zephyr_usb_bulk_cycle_count_fn cycle_counter;
  wl_rx_unit_claim_t rx_unit_claim;
  wl_io_token_t tx_token;
  /* Endpoint addresses captured when each outstanding request is queued. */
  uint8_t rx_endpoint;
  uint8_t tx_endpoint;
  atomic_t flags;
  atomic_t tx_completion;
  atomic_t rx_claims;
  atomic_t rx_completions;
  atomic_t rx_bytes;
  atomic_t rx_pauses;
  atomic_t tx_submissions;
  atomic_t tx_completions;
  atomic_t tx_bytes;
  atomic_t rx_callback_cycles;
  atomic_t rx_callback_max_cycles;
  atomic_t tx_callback_cycles;
  atomic_t tx_callback_max_cycles;
  atomic_t tx_sink_cycles;
  atomic_t tx_sink_max_cycles;
  atomic_t active_service_calls;
  atomic_t active_service_cycles;
  atomic_t active_service_max_cycles;
  atomic_t errors;
  atomic_t rx_fault;
  atomic_t last_rx_fault;
} wl_zephyr_usb_bulk_t;

/* Initialize before usbd_init()/usbd_enable(). One instance is supported. */
int wl_zephyr_usb_bulk_init(wl_zephyr_usb_bulk_t *adapter,
                            const wl_zephyr_usb_bulk_config_t *config);

/* Call from Wirelink's single-consumer context after wl_poll(). */
int wl_zephyr_usb_bulk_service(wl_zephyr_usb_bulk_t *adapter);
wl_readiness_t wl_zephyr_usb_bulk_readiness(const wl_zephyr_usb_bulk_t *adapter);
wl_usb_bulk_rx_fault_t wl_zephyr_usb_bulk_rx_fault(
    const wl_zephyr_usb_bulk_t *adapter);
/* Owner/thread context only, outside USB callbacks. Disable and re-enable the
 * entire USBD context so the host must enumerate/configure again. The fault
 * remains latched until that configuration enables this class. No core/RPC
 * session or queued/leased payload is silently discarded. */
int wl_zephyr_usb_bulk_recover(wl_zephyr_usb_bulk_t *adapter);
/* Clear telemetry counters without changing endpoint ownership. */
void wl_zephyr_usb_bulk_reset_stats(wl_zephyr_usb_bulk_t *adapter);
void wl_zephyr_usb_bulk_get_stats(const wl_zephyr_usb_bulk_t *adapter,
                                  wl_zephyr_usb_bulk_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* WIRELINK_ZEPHYR_USB_BULK_H_ */
