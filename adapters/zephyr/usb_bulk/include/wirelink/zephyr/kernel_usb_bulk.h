/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_KERNEL_USB_BULK_H_
#define WIRELINK_KERNEL_USB_BULK_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/atomic.h>

#include "wirelink/packet_port.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initial descriptor addresses only. Zephyr may reassign them in usbd_init(). */
#define WL_KERNEL_USB_BULK_OUT_EP 0x01U
#define WL_KERNEL_USB_BULK_IN_EP 0x81U
#define WL_KERNEL_USB_BULK_INTERFACE_SUBCLASS 0x57U
#define WL_KERNEL_USB_BULK_INTERFACE_PROTOCOL 0x4CU

typedef uint32_t (*wl_kernel_usb_bulk_cycle_count_fn)(void *user_data);
typedef void (*wl_kernel_usb_bulk_wake_fn)(void *user_data);

typedef enum {
  WL_KERNEL_USB_RX_FAULT_NONE = 0,
  WL_KERNEL_USB_RX_FAULT_BOUNDARY_LOST,
  WL_KERNEL_USB_RX_FAULT_TRANSFER,
  WL_KERNEL_USB_RX_FAULT_INVALID_COMPLETION,
  WL_KERNEL_USB_RX_FAULT_QUEUE,
} wl_kernel_usb_rx_fault_t;

typedef struct wl_kernel_usb_bulk_config {
  wl_packet_port_t *port;
  size_t maximum_frame_size; /* Strictly less than the supplied RX unit size. */
  /* Optional ISR-safe notification for the single Wirelink consumer. */
  void *wake_user_data;
  wl_kernel_usb_bulk_wake_fn wake_consumer;
  /* Optional thread/ISR-safe cycle counter used only for instrumentation. */
  void *cycle_counter_user_data;
  wl_kernel_usb_bulk_cycle_count_fn cycle_counter;
} wl_kernel_usb_bulk_config_t;

typedef struct wl_kernel_usb_bulk_stats {
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
  wl_kernel_usb_rx_fault_t rx_fault;
  wl_kernel_usb_rx_fault_t last_rx_fault;
  bool recovering;
} wl_kernel_usb_bulk_stats_t;

typedef struct wl_kernel_usb_bulk {
  wl_packet_port_t *port;
  size_t maximum_rx_size;
  void *wake_user_data;
  wl_kernel_usb_bulk_wake_fn wake_consumer;
  void *cycle_counter_user_data;
  wl_kernel_usb_bulk_cycle_count_fn cycle_counter;
  wl_packet_claim_t rx_unit_claim;
  wl_packet_token_t tx_token;
  /* Endpoint addresses captured when each outstanding request is queued. */
  uint8_t rx_endpoint;
  uint8_t tx_endpoint;
  atomic_t flags;
  atomic_t callbacks_active;
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
} wl_kernel_usb_bulk_t;

/* Init port first with submit/notify below and user=adapter, then bind before
 * usbd_init()/usbd_enable(). One instance. Does not initialize/clear RX storage.
 * The prototype shares the original USB class name, so compile only this adapter. */
wl_packet_submit_t wl_kernel_usb_bulk_submit(void *, const uint8_t *, size_t,
                                            wl_packet_token_t);
void wl_kernel_usb_bulk_notify(void *);
/* stop is owner/thread only. It stops port admission and disables USBD, but
 * buffers remain leased until real cancellation/completion callbacks arrive.
 * stopped is the adapter's physical acknowledgement, independent of pending
 * logical callbacks/RX borrows. Deinit requires both physical and port silence.
 * New init must use a fresh instance ID; no callbacks may access the old port. */
int wl_kernel_usb_bulk_stop(wl_kernel_usb_bulk_t *);
int wl_kernel_usb_bulk_stopped(const wl_kernel_usb_bulk_t *);
int wl_kernel_usb_bulk_deinit(wl_kernel_usb_bulk_t *);

int wl_kernel_usb_bulk_init(wl_kernel_usb_bulk_t *adapter,
                            const wl_kernel_usb_bulk_config_t *config);

/* Owner only; call before/after kernel step to publish resource progress. */
int wl_kernel_usb_bulk_service(wl_kernel_usb_bulk_t *adapter);
int wl_kernel_usb_bulk_ready(const wl_kernel_usb_bulk_t *adapter);
wl_kernel_usb_rx_fault_t wl_kernel_usb_bulk_rx_fault(
    const wl_kernel_usb_bulk_t *adapter);
/* Owner/thread context only, outside USB callbacks. Disable and re-enable the
 * entire USBD context so the host must enumerate/configure again. The fault
 * remains latched until that configuration enables this class. No core/RPC
 * session or queued/leased payload is silently discarded. */
int wl_kernel_usb_bulk_recover(wl_kernel_usb_bulk_t *adapter);
/* Clear telemetry counters without changing endpoint ownership. */
void wl_kernel_usb_bulk_reset_stats(wl_kernel_usb_bulk_t *adapter);
void wl_kernel_usb_bulk_get_stats(const wl_kernel_usb_bulk_t *adapter,
                                  wl_kernel_usb_bulk_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* WIRELINK_KERNEL_USB_BULK_H_ */
