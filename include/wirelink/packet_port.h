/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_PACKET_PORT_H_
#define WIRELINK_PACKET_PORT_H_

#ifndef __cplusplus
#include <stdatomic.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include "wirelink/span.h"
#include "wirelink/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WL_PACKET_RX_SLOTS 2U
#define WL_PACKET_SEQUENCE_MAX UINT32_C(0x0fffffff)

typedef struct {
  uint32_t instance;
  uint32_t sequence;
} wl_packet_token_t;

typedef struct {
  wl_span_t span;
  wl_packet_token_t token;
} wl_packet_claim_t;

typedef struct {
  const uint8_t *data;
  size_t length;
  wl_packet_token_t token;
} wl_packet_borrow_t;

typedef int32_t wl_packet_submit_t;
/* Stable C ABI even when a consumer uses -fshort-enums. */
enum {
  WL_PACKET_SENT,    /* Source consumed and physical transfer drained. */
  WL_PACKET_STARTED, /* Completion will be published, possibly before return. */
  WL_PACKET_BUSY,    /* Nothing accepted; adapter must publish resource activity. */
  WL_PACKET_FAILED  /* Nothing accepted; no future completion. */
};

enum {
  WL_PACKET_CONSUMED = 2U,
  WL_PACKET_DRAINED = 4U,
  WL_PACKET_TX_ERROR = 8U
};

typedef wl_packet_submit_t (*wl_packet_sink_fn)(
    void *, const uint8_t *, size_t, wl_packet_token_t);
typedef void (*wl_packet_notify_fn)(void *);

/* Adapter binding for generated nodes. A zero RX triple selects the node's
 * built-in storage. External storage must remain live until I/O is stopped
 * and all claims and leases have drained. */
typedef struct {
  wl_packet_sink_fn submit; /* Required for nodes with a TX role. */
  wl_packet_notify_fn notify; /* Optional owner wakeup. */
  void *user;
  uint8_t *rx_storage;
  size_t rx_storage_size;
  size_t rx_unit_size;
} wl_packet_io_t;

typedef struct {
  uint32_t instance; /* Nonzero, unique across reinitialization. */
  uint8_t *rx_storage;
  size_t rx_unit_size;
  size_t rx_storage_size;
  wl_packet_sink_fn submit;
  wl_packet_notify_fn notify; /* Latched outer event; callable from I/O context. */
  void *user;
} wl_packet_config_t;

/* One serialized producer and one owner. All fields are private to port.c.
 * init/reinit requires stopped I/O and no outstanding borrow or physical lease.
 * Only publish/claim/commit/abort/activity may run in the producer/I/O context.
 * No codec, handler, allocation, clock or owner callback runs here. */
/* C++ storage uses the qualified C ABI representation. All atomic access and
 * initialization belongs to packet_port.c; do not access or copy live fields
 * from C++. Supported toolchains must pass the cross-language layout test. */
#ifdef __cplusplus
typedef uint_least32_t wl_atomic_u32_t;
typedef unsigned char wl_atomic_bool_t;
#else
typedef atomic_uint_least32_t wl_atomic_u32_t;
typedef atomic_bool wl_atomic_bool_t;
#endif
typedef struct wl_packet_port wl_packet_port_t;
struct wl_packet_port {
  wl_packet_config_t config;
  wl_atomic_u32_t write_cursor;
  wl_atomic_u32_t read_cursor;
  wl_atomic_u32_t tx_state;
  wl_atomic_bool_t running;
  wl_atomic_bool_t claim_active;
  wl_atomic_bool_t activity;
  size_t lengths[WL_PACKET_RX_SLOTS];
  uint32_t rx_sequences[WL_PACKET_RX_SLOTS];
  uint32_t claim_sequence;
  uint32_t claim_cursor;
  size_t claim_capacity;
  uint32_t tx_sequence;
  unsigned tx_delivered;
  uint8_t borrowed;
};

size_t wl_packet_rx_unit_size(const wl_packet_port_t *);
/* Allocate a nonzero identity unique among automatic allocations in this
 * process. Does not survive reboot: persistent transports must supply a fresh
 * explicit identity. Never wraps; exhaustion returns WL_ERR_ID_EXHAUSTED. */
wl_err_t wl_packet_instance_next(uint32_t *out);
wl_err_t wl_packet_init(wl_packet_port_t *, const wl_packet_config_t *);
/* Single-producer copy admission. On failure no new claim remains held. */
WL_HOT_ATTR wl_err_t wl_packet_rx_push(wl_packet_port_t *, const uint8_t *, size_t);
WL_HOT_ATTR wl_err_t wl_packet_rx_claim(wl_packet_port_t *, size_t, wl_packet_claim_t *);
WL_HOT_ATTR wl_err_t wl_packet_rx_commit(wl_packet_port_t *, const wl_packet_claim_t *, size_t);
wl_err_t wl_packet_rx_abort(wl_packet_port_t *, const wl_packet_claim_t *);
WL_HOT_ATTR wl_err_t wl_packet_rx_borrow(wl_packet_port_t *, wl_packet_borrow_t *);
WL_HOT_ATTR wl_err_t wl_packet_rx_release(wl_packet_port_t *, const wl_packet_borrow_t *);
WL_HOT_ATTR int wl_packet_rx_ready(const wl_packet_port_t *);
/* Producer/owner hint only: the next claim is still authoritative. */
WL_HOT_ATTR int wl_packet_rx_available(const wl_packet_port_t *);

/* Owner only. BUSY/FAILED do not retain the source. STARTED/SENT retain it
 * until owner receives CONSUMED. No second physical TX before DRAINED. */
WL_HOT_ATTR wl_err_t wl_packet_tx_submit(wl_packet_port_t *, const uint8_t *, size_t,
                                         wl_packet_token_t *);
/* I/O only. DRAINED implies CONSUMED; TX_ERROR implies both. Duplicate,
 * stale-instance and stale-transfer publications return NOT_FOUND. */
WL_HOT_ATTR wl_err_t wl_packet_tx_publish(wl_packet_port_t *, wl_packet_token_t, unsigned);
/* Owner only. Returns each new completion bit once. */
WL_HOT_ATTR unsigned wl_packet_tx_take(wl_packet_port_t *, wl_packet_token_t *);
WL_HOT_ATTR int wl_packet_tx_active(const wl_packet_port_t *);
WL_HOT_ATTR int wl_packet_tx_ready(const wl_packet_port_t *);

WL_HOT_ATTR void wl_packet_activity(wl_packet_port_t *);
WL_HOT_ATTR int wl_packet_take_activity(wl_packet_port_t *);
int wl_packet_activity_pending(const wl_packet_port_t *);
void wl_packet_stop(wl_packet_port_t *);
/* Does not mean the adapter is stopped: it must separately acknowledge that
 * it will no longer access this port before reinitialization. */
int wl_packet_quiescent(const wl_packet_port_t *);

#ifdef __cplusplus
}
#endif

#endif
