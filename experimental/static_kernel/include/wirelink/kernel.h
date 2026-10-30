/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_KERNEL_H_
#define WIRELINK_KERNEL_H_

#include "wirelink/kernel_wire.h"
#include "wirelink/packet_port.h"

#define WL_KERNEL_CONTRACT 1U
#define WL_KERNEL_CALLS 4U
#define WL_KERNEL_REPLIES 4U
#define WL_KERNEL_TX_COUNT 9U
#define WL_KERNEL_BUDGET 16U

typedef enum {
  WL_KERNEL_SUCCESS,
  WL_KERNEL_REJECTED,
  WL_KERNEL_LOCAL_ERROR,
  WL_KERNEL_TIMED_OUT,
  WL_KERNEL_CANCELLED,
  WL_KERNEL_CLOSED
} wl_kernel_outcome_t;

typedef struct {
  wl_kernel_outcome_t outcome;
  int32_t status; /* Remote business rejection or local wl_err_t, respectively. */
} wl_kernel_result_t;

typedef struct {
  uint64_t instance;
  uint32_t generation;
  uint32_t operation;
  uint8_t slot;
} wl_kernel_call_handle_t;

typedef struct wl_kernel wl_kernel_t;
typedef struct wl_kernel_call wl_kernel_call_t;
typedef wl_err_t (*wl_kernel_encode_fn)(const void *, uint8_t *, size_t, size_t *);
/* Generated completion decodes once and invokes the saved typed callback.
 * callback is converted back to its original function-pointer type before use,
 * as permitted by C11 6.3.2.3. It is never called through this erased type. */
typedef void (*wl_kernel_complete_fn)(wl_kernel_t *, const wl_kernel_call_t *,
                                      wl_kernel_result_t, const uint8_t *, size_t);
typedef wl_err_t (*wl_kernel_route_fn)(wl_kernel_t *, const wl_kernel_frame_t *);
typedef void (*wl_kernel_error_fn)(void *, uint16_t, wl_err_t);

struct wl_kernel_call {
  wl_kernel_complete_fn complete;
  void (*callback)(void);
  void *user;
  uint32_t operation;
  uint32_t generation;
  uint32_t deadline;
  uint16_t response_id;
  wl_kernel_result_t result;
  uint8_t state;
  uint8_t sent;
};

typedef struct {
  wl_span_t storage;
  size_t length;
  uint32_t generation;
  uint16_t message_id;
  uint8_t state;
  uint8_t call_slot;
} wl_kernel_tx_t;

typedef struct {
  wl_packet_port_t *port;
  /* Four request frames, four reply frames, one ordinary message frame.
   * Generated layout sizes these by their own codec bounds, not a global max. */
  wl_span_t tx_storage[WL_KERNEL_TX_COUNT];
  wl_kernel_route_fn route;
  wl_kernel_error_fn on_error;
  void *user;
} wl_kernel_config_t;

/* Single owner, including all public operations and callbacks. I/O publishes
 * only through packet_port. No public operation calls terminal callbacks
 * inline except response dispatch during step. All fields are private. */
struct wl_kernel {
  wl_packet_port_t *port;
  wl_kernel_route_fn route;
  wl_kernel_error_fn on_error;
  void *user;
  wl_kernel_call_t calls[WL_KERNEL_CALLS];
  wl_kernel_tx_t tx[WL_KERNEL_TX_COUNT];
  wl_packet_borrow_t rx;
  wl_packet_token_t flight_token;
  uint32_t operation;
  uint8_t flight;
  uint8_t tx_cursor;
  uint8_t call_cursor;
  uint8_t rx_held;
  uint8_t rx_blocked;
  uint8_t tx_blocked;
  uint8_t in_step;
  uint8_t closed;
};

wl_err_t wl_kernel_init(wl_kernel_t *, const wl_kernel_config_t *);
/* timeout is 1..INT32_MAX ticks; now comes from the owner. Accepted requests
 * are encoded before return. Any admission failure creates no callback. */
wl_err_t wl_kernel_call_start(wl_kernel_t *, uint16_t request_id,
                              uint16_t response_id, const void *, wl_kernel_encode_fn,
                              uint32_t now, uint32_t timeout, wl_kernel_complete_fn,
                              void (*callback)(void), void *user,
                              wl_kernel_call_handle_t *);
wl_err_t wl_kernel_cancel(wl_kernel_t *, wl_kernel_call_handle_t);
wl_err_t wl_kernel_send(wl_kernel_t *, uint16_t, const void *, wl_kernel_encode_fn);
wl_err_t wl_kernel_step(wl_kernel_t *, uint32_t now, unsigned budget);
/* Runnability and earliest client deadline only. Outer runtime must use a
 * latched event and check activity again across query/wait. */
int wl_kernel_ready(const wl_kernel_t *, uint32_t now,
                     int *has_deadline, uint32_t *deadline);
wl_err_t wl_kernel_close(wl_kernel_t *);
int wl_kernel_quiescent(const wl_kernel_t *);

/* Generated routing internals: reserve before decoding/handler side effects.
 * These are owner-step scoped, never application reply tokens or late finish. */
wl_err_t wl_kernel_reply_reserve(wl_kernel_t *, unsigned *);
void wl_kernel_reply_abort(wl_kernel_t *, unsigned);
wl_err_t wl_kernel_reply_finish(wl_kernel_t *, unsigned, uint16_t,
                                const wl_kernel_rpc_metadata_t *, int32_t,
                                const void *, wl_kernel_encode_fn);
wl_err_t wl_kernel_response(wl_kernel_t *, uint16_t, const uint8_t *, size_t);

#endif
