/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_UDP_H_
#define WIRELINK_ZEPHYR_UDP_H_

#include "wirelink/adapter.h"
#include "wirelink/endpoint.h"
#include "wirelink/frame.h"
#include "wirelink/port.h"
#include "wirelink/zephyr/detail/udp_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zero means default for optional fields. Numeric IPv4 only. Strings are read
 * during open, not retained. Configure the network interface separately. */
typedef struct {
  const char *peer_address;          /* Required, fixed until close. */
  uint16_t peer_port;                /* Required, nonzero. */
  const char *local_address;         /* NULL = 0.0.0.0. */
  uint16_t local_port;               /* Zero = ephemeral port. */
  size_t service_budget;            /* Zero = 8 datagrams, including rejects. */
  uint32_t tx_retry_ms;              /* Zero = 1 ms, only during backpressure. */
  size_t maximum_datagram_size;      /* Zero = 1472 (1500-byte IPv4 MTU).
                                      Limit for the whole Wirelink frame. */
} wl_zephyr_udp_config_t;

#ifdef CONFIG_WIRELINK_ZEPHYR_UDP_TIMING
/* Elapsed system-timer cycles, not necessarily CPU cycles. Includes preemption
 * and blocking. Each interval must fit within one 32-bit counter period. */
typedef struct {
  uint64_t calls;
  uint64_t cycles;
  uint32_t max_cycles;
} wl_zephyr_udp_timing_t;
#endif

typedef struct {
  wl_adapter_stats_t common;
  uint64_t rx_rejected;
  uint64_t tx_backpressure;          /* Actual socket BUSY results. */
  uint64_t tx_deferred;              /* Sink attempts gated before retry due. */
  uint64_t service_budget_hits;
  uint64_t rx_idle_passes;           /* First receive found no datagram. */
  uint64_t wait_calls;
  uint64_t wait_timeouts;
  wl_err_t last_error;               /* Latched fatal I/O error; reopen clears. */
#ifdef CONFIG_WIRELINK_ZEPHYR_UDP_TIMING
  wl_zephyr_udp_timing_t send_timing;    /* Actual send attempts, not gated BUSY. */
  wl_zephyr_udp_timing_t receive_timing; /* Includes empty/rejected receives. */
  wl_zephyr_udp_timing_t service_timing; /* RX service only, not whole step. */
#endif
} wl_zephyr_udp_stats_t;

/* Declare with WL_ZEPHYR_UDP_DEFINE. All fields are private; keep at a stable
 * address. No heap or thread is created by this adapter. The network stack
 * still uses its configured packet, socket and eventfd pools. The tested
 * Zephyr native send path can wait ~1 s on pool exhaustion despite DONTWAIT;
 * this is not a hard-real-time API (see docs/zephyr-udp.md). */
typedef struct {
  struct {
    uint8_t *rx_storage;
    size_t payload_capacity;
    uint8_t slot_count;
  } private_storage;
  struct {
    wl_zephyr_udp_socket_state_t io;
    wl_endpoint_t *endpoint;
    wl_zephyr_udp_stats_t stats;
    atomic_t notifications;
    size_t maximum_frame;
    size_t service_budget;
    uint32_t retry_ms;
    wl_time_ms_t retry_due;
    uint16_t local_port;
    bool retry_pending;
    bool more_rx;
    bool rx_paused;
  } private_state;
} wl_zephyr_udp_t;

#ifdef __cplusplus
#define WL_ZEPHYR_UDP_ZERO_STATE {}
#else
#define WL_ZEPHYR_UDP_ZERO_STATE { .io = {0} }
#endif

/* File or function scope, static lifetime. Use the generated
 * <NAME>_ENDPOINT_MAX_PAYLOAD constant; only native RX slots are reserved.
 * The extra byte per slot detects truncation and is not sent on the wire. */
#define WL_ZEPHYR_UDP_DEFINE(name, payload_bound, receive_slots)                \
  BUILD_ASSERT((payload_bound) > 0 && (payload_bound) <= WL_FRAME_MAX_PAYLOAD,  \
               "UDP payload bound exceeds Wirelink limits");                \
  BUILD_ASSERT((receive_slots) >= 2 &&                                        \
               (receive_slots) <= WL_RX_UNIT_QUEUE_MAX_SLOTS,                 \
               "UDP receive slots must be in [2, 8]");                      \
  static uint8_t name##_rx_storage[((payload_bound) + WL_FRAME_HEADER_SIZE +  \
      WL_FRAME_MAX_CRC + 1U) * (receive_slots)];                              \
  static wl_zephyr_udp_t name = {                                            \
    { name##_rx_storage, (payload_bound), (receive_slots) },                  \
    WL_ZEPHYR_UDP_ZERO_STATE }

/* Owner only, before first step/send and any other ingress on a fresh endpoint.
 * Automatically binds sink, service, deadline, quiesce and RPC waiter.
 * An existing adapter or waiter is rejected, never silently replaced.
 * Only native packet is supported; both peers must select it explicitly. */
wl_err_t wl_zephyr_udp_open(wl_zephyr_udp_t *adapter, wl_endpoint_t *endpoint,
                           const wl_zephyr_udp_config_t *config);
/* Between owner passes only, never from a business callback. Merges endpoint
 * work/deadlines with maximum_ms (UINT32_MAX means
 * unbounded). Does not receive, step or dispatch callbacks. Step after return.
 * A manually advanced clock must notify this waiter when advanced. */
wl_err_t wl_zephyr_udp_wait(wl_zephyr_udp_t *adapter, uint32_t maximum_ms);
/* Producer THREADS only, not ISR-safe. Business queue synchronization remains
 * the application's/executor's responsibility. Join producers before close. */
wl_err_t wl_zephyr_udp_notify(wl_zephyr_udp_t *adapter);
wl_err_t wl_zephyr_udp_request_stop(wl_zephyr_udp_t *adapter);
/* Close the GENERATED endpoint first, then join notification producers, then
 * close this adapter. Closing an attached adapter returns BUSY. Endpoint close
 * stops I/O but deliberately keeps descriptors until producer threads join.
 * Idempotent; safe after endpoint storage has been reinitialized elsewhere. */
wl_err_t wl_zephyr_udp_close(wl_zephyr_udp_t *adapter);
uint16_t wl_zephyr_udp_local_port(const wl_zephyr_udp_t *adapter);
/* Owner-only snapshot. activity_notifications is a wrapping 32-bit count;
 * other counters are owner-local 64-bit. No concurrent reset interface. */
wl_err_t wl_zephyr_udp_get_stats(const wl_zephyr_udp_t *adapter,
                                wl_zephyr_udp_stats_t *stats);

#ifdef __cplusplus
}
#endif
#endif
