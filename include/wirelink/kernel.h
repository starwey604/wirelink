/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_KERNEL_H_
#define WIRELINK_KERNEL_H_

#include "wirelink/kernel_wire.h"
#include "wirelink/packet_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WL_KERNEL_CONTRACT 1U
#define WL_KERNEL_CALLS 4U
#define WL_KERNEL_REPLIES 3U
#define WL_KERNEL_TX_COUNT 8U
#define WL_KERNEL_TX_MASK (WL_KERNEL_TX_COUNT - 1U)
#define WL_KERNEL_CALLS_MASK (WL_KERNEL_CALLS - 1U)
#define WL_KERNEL_BUDGET 16U

typedef int32_t wl_kernel_outcome_t;
/* Stable C ABI even when a consumer uses -fshort-enums. */
enum {
  WL_KERNEL_SUCCESS,
  WL_KERNEL_REJECTED,
  WL_KERNEL_LOCAL_ERROR,
  WL_KERNEL_TIMED_OUT,
  WL_KERNEL_CANCELLED,
  WL_KERNEL_CLOSED
};

typedef struct {
  wl_kernel_outcome_t outcome;
  int32_t status; /* Remote business rejection or local wl_err_t, respectively. */
} wl_kernel_result_t;

typedef struct {
  uint32_t instance;
  uint32_t generation;
  uint32_t operation;
  uint8_t slot;
} wl_kernel_call_handle_t;

typedef struct wl_kernel wl_kernel_t;
typedef struct wl_kernel_call wl_kernel_call_t;
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
  uint32_t generation;
  uint16_t length;
  uint16_t message_id;
  uint8_t state;
  uint8_t call_slot;
} wl_kernel_tx_t;

/* Contiguous per-category TX pools. Generated layout sizes each category by
 * its own codec bounds, avoiding a single global max across all categories. */
typedef struct {
  uint8_t *requests;
  uint8_t *replies;
  uint8_t *message;
  uint16_t request_stride;
  uint16_t reply_stride;
  uint16_t message_capacity;
} wl_kernel_storage_t;

static inline wl_kernel_storage_t wl_kernel_storage_uniform(uint8_t *base,
                                                            uint16_t stride) {
  wl_kernel_storage_t storage = {
      base,
      base != NULL ? base + (size_t)WL_KERNEL_CALLS * stride : NULL,
      base != NULL ? base + (size_t)(WL_KERNEL_CALLS + WL_KERNEL_REPLIES) * stride : NULL,
      stride,
      stride,
      stride,
  };
  return storage;
}

typedef struct {
  wl_packet_port_t *port;
  wl_kernel_storage_t storage;
  wl_kernel_route_fn route;
  wl_kernel_error_fn on_error;
  void *user;
} wl_kernel_config_t;

/* Single owner, including all public operations and callbacks. I/O publishes
 * only through packet_port. No public operation calls terminal callbacks
 * inline except response dispatch during step. All fields are private. */
struct wl_kernel {
  wl_packet_port_t *port;
  wl_kernel_storage_t storage;
  wl_kernel_route_fn route;
  wl_kernel_error_fn on_error;
  void *user;
  wl_kernel_call_t calls[WL_KERNEL_CALLS];
  wl_kernel_tx_t tx[WL_KERNEL_TX_COUNT];
  wl_packet_borrow_t rx;
  wl_packet_token_t flight_token;
  uint32_t operation;
  uint8_t flight;
  uint8_t tx_queue[WL_KERNEL_TX_COUNT];
  uint8_t tx_head;
  uint8_t tx_count;
  uint8_t call_cursor;
  uint8_t rx_held;
  uint8_t rx_blocked;
  uint8_t tx_blocked;
  uint8_t rx_turn;
  uint8_t in_step;
  uint8_t closed;
};

wl_err_t wl_kernel_init(wl_kernel_t *, const wl_kernel_config_t *);

/* Zero-callback ordinary message send: claim frame buffer, encode at
 * frame + WL_KERNEL_NATIVE_HEADER, seal 4B header, and commit total length. */
WL_HOT_ATTR wl_err_t wl_kernel_msg_claim(wl_kernel_t *, uint8_t **out_frame,
                                         size_t *out_capacity);
WL_HOT_ATTR wl_err_t wl_kernel_msg_commit(wl_kernel_t *, size_t frame_length);

/* Zero-callback RPC request: claim slot + payload buffer (after 12B headers),
 * encode request body, then commit with timeout (1..INT32_MAX ticks). */
WL_HOT_ATTR wl_err_t wl_kernel_call_claim(wl_kernel_t *, uint8_t *out_slot,
                                          uint8_t **out_payload,
                                          size_t *out_capacity);
WL_HOT_ATTR wl_err_t wl_kernel_call_commit(
    wl_kernel_t *, uint8_t slot, uint16_t request_id, uint16_t response_id,
    size_t body_length, uint32_t now, uint32_t timeout, wl_kernel_complete_fn,
    void (*callback)(void), void *user, wl_kernel_call_handle_t *);

wl_err_t wl_kernel_cancel(wl_kernel_t *, wl_kernel_call_handle_t);

/* budget is 1..WL_KERNEL_BUDGET events. Eligible terminals precede RX;
 * RX/TX turns persist across calls so even budget=1 makes progress both ways. */
WL_HOT_ATTR wl_err_t wl_kernel_step(wl_kernel_t *, uint32_t now, unsigned budget);
/* Runnability and earliest client deadline only. Outer runtime must use a
 * latched event and check activity again across query/wait. */
WL_HOT_ATTR int wl_kernel_ready(const wl_kernel_t *, uint32_t now,
                                int *has_deadline, uint32_t *deadline);
wl_err_t wl_kernel_close(wl_kernel_t *);
int wl_kernel_quiescent(const wl_kernel_t *);

/* Generated routing internals: claim before decoding/handler side effects.
 * These are owner-step scoped, never application reply tokens or late finish. */
WL_HOT_ATTR wl_err_t wl_kernel_reply_claim(
    wl_kernel_t *, uint16_t response_id,
    const wl_kernel_rpc_metadata_t *request, uint8_t *out_slot,
    uint8_t **out_payload, size_t *out_capacity);
WL_HOT_ATTR void wl_kernel_reply_abort(wl_kernel_t *, uint8_t slot);
WL_HOT_ATTR wl_err_t wl_kernel_reply_commit(wl_kernel_t *, uint8_t slot,
                                            size_t body_length);
WL_HOT_ATTR wl_err_t wl_kernel_reply_error(wl_kernel_t *, uint8_t slot,
                                           int32_t status);
WL_HOT_ATTR wl_err_t wl_kernel_response(wl_kernel_t *, const wl_kernel_frame_t *);

#ifdef __cplusplus
}
#endif

#endif
