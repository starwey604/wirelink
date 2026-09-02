/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_ZEPHYR_USB_BULK_H_
#define WIRELINK_ZEPHYR_USB_BULK_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/atomic.h>

#include "wirelink/wirelink.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WL_ZEPHYR_USB_BULK_OUT_EP 0x01U
#define WL_ZEPHYR_USB_BULK_IN_EP 0x81U
#define WL_ZEPHYR_USB_BULK_INTERFACE_SUBCLASS 0x57U
#define WL_ZEPHYR_USB_BULK_INTERFACE_PROTOCOL 0x4CU

typedef uint32_t (*wl_zephyr_usb_bulk_cycle_count_fn)(void *user_data);
typedef void (*wl_zephyr_usb_bulk_wake_fn)(void *user_data);

typedef struct wl_zephyr_usb_bulk_config {
  wl_ctx_t *link;
  size_t maximum_rx_size;
  /* Required only for WL_ENVELOPE_NATIVE_PACKET zero-copy RX. */
  uint8_t *unit_queue_storage;
  size_t unit_queue_storage_size;
  uint8_t unit_queue_slots;
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
} wl_zephyr_usb_bulk_stats_t;

typedef struct wl_zephyr_usb_bulk {
  wl_ctx_t *link;
  size_t maximum_rx_size;
  void *wake_user_data;
  wl_zephyr_usb_bulk_wake_fn wake_consumer;
  void *cycle_counter_user_data;
  wl_zephyr_usb_bulk_cycle_count_fn cycle_counter;
  wl_rx_dma_claim_t rx_claim;
  wl_rx_unit_claim_t rx_unit_claim;
  wl_io_token_t tx_token;
  bool native_unit_mode;
  atomic_t flags;
  atomic_t tx_completion;
  atomic_t rx_claims;
  atomic_t rx_completions;
  atomic_t rx_bytes;
  atomic_t rx_pauses;
  atomic_t tx_submissions;
  atomic_t tx_completions;
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
} wl_zephyr_usb_bulk_t;

/* Initialize before usbd_init()/usbd_enable(). One instance is supported. */
int wl_zephyr_usb_bulk_init(wl_zephyr_usb_bulk_t *adapter,
                            const wl_zephyr_usb_bulk_config_t *config);

/* Call from Wirelink's single-consumer context after wl_poll(). */
int wl_zephyr_usb_bulk_service(wl_zephyr_usb_bulk_t *adapter);
/* Clear telemetry counters without changing endpoint ownership. */
void wl_zephyr_usb_bulk_reset_stats(wl_zephyr_usb_bulk_t *adapter);
void wl_zephyr_usb_bulk_get_stats(const wl_zephyr_usb_bulk_t *adapter,
                                  wl_zephyr_usb_bulk_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* WIRELINK_ZEPHYR_USB_BULK_H_ */
