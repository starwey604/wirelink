/* SPDX-License-Identifier: Apache-2.0 */

#include "rx_ring.h"

#include <limits.h>
#include <stdatomic.h>
#include <string.h>

#ifdef LWRB_DISABLE_ATOMIC
#error "Wirelink's LwRB backend requires C11 atomic indices"
#endif

#include "lwrb/lwrb.h"
#include "wirelink/types.h"

typedef struct {
  lwrb_t buffer;
  _Atomic unsigned int overflow_events;
  size_t reserved_length;
  uint8_t reservation_active;
} wl_rx_lwrb_state_t;

_Static_assert(sizeof(wl_rx_lwrb_state_t) <= WL_RX_RING_STATE_SIZE,
               "WL_RX_RING_STATE_SIZE is too small for the LwRB backend");

static wl_rx_lwrb_state_t *rx_state(wl_rx_ring_state_t *state) {
  return (wl_rx_lwrb_state_t *)(void *)state->bytes;
}

static const wl_rx_lwrb_state_t *
rx_state_const(const wl_rx_ring_state_t *state) {
  return (const wl_rx_lwrb_state_t *)(const void *)state->bytes;
}

size_t wl_rx_ring_storage_size(size_t usable_capacity) {
  if (usable_capacity == 0U || usable_capacity > (size_t)ULONG_MAX - 1U) {
    return 0U;
  }
  return usable_capacity + 1U;
}

int wl_rx_ring_init(wl_rx_ring_state_t *state, uint8_t *memory,
                    size_t memory_size) {
  wl_rx_lwrb_state_t *backend;

  if (state == NULL || memory == NULL || memory_size < 2U ||
      memory_size > (size_t)ULONG_MAX) {
    return WL_ERR_INVALID_ARG;
  }

  memset(state, 0, sizeof(*state));
  backend = rx_state(state);
  if (lwrb_init(&backend->buffer, memory, (lwrb_sz_t)memory_size) == 0U) {
    return WL_ERR_INVALID_ARG;
  }
  atomic_init(&backend->overflow_events, 0U);
  if (!atomic_is_lock_free(&backend->buffer.r_ptr) ||
      !atomic_is_lock_free(&backend->buffer.w_ptr) ||
      !atomic_is_lock_free(&backend->overflow_events)) {
    lwrb_free(&backend->buffer);
    return WL_ERR_NOT_SUPPORTED;
  }
  return WL_OK;
}

void wl_rx_ring_producer_note_overflow(wl_rx_ring_state_t *state) {
  wl_rx_lwrb_state_t *backend;

  if (state == NULL) {
    return;
  }
  backend = rx_state(state);
  (void)atomic_fetch_add_explicit(&backend->overflow_events, 1U,
                                  memory_order_release);
}

int wl_rx_ring_producer_reserve(wl_rx_ring_state_t *state,
                                wl_span_t *out_span) {
  wl_rx_lwrb_state_t *backend;

  if (state == NULL || out_span == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  backend = rx_state(state);
  if (backend->reservation_active != 0U) {
    return WL_ERR_INVALID_STATE;
  }

  out_span->data =
      (uint8_t *)lwrb_get_linear_block_write_address(&backend->buffer);
  out_span->length =
      (size_t)lwrb_get_linear_block_write_length(&backend->buffer);
  backend->reserved_length = out_span->length;
  backend->reservation_active = 1U;
  return WL_OK;
}

int wl_rx_ring_producer_commit(wl_rx_ring_state_t *state, size_t length) {
  wl_rx_lwrb_state_t *backend;
  lwrb_sz_t advanced;

  if (state == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  backend = rx_state(state);
  if (backend->reservation_active == 0U) {
    return WL_ERR_INVALID_STATE;
  }
  if (length > backend->reserved_length) {
    return WL_ERR_INVALID_ARG;
  }

  if (length == 0U) {
    advanced = 0U;
  } else {
    advanced = lwrb_advance(&backend->buffer, (lwrb_sz_t)length);
  }
  if ((size_t)advanced != length) {
    return WL_ERR_INVALID_STATE;
  }
  backend->reservation_active = 0U;
  backend->reserved_length = 0U;
  return WL_OK;
}

size_t wl_rx_ring_readable(const wl_rx_ring_state_t *state) {
  if (state == NULL) {
    return 0U;
  }
  return (size_t)lwrb_get_full(&rx_state_const(state)->buffer);
}

wl_span_t wl_rx_ring_consumer_peek(wl_rx_ring_state_t *state) {
  wl_rx_lwrb_state_t *backend;

  if (state == NULL) {
    return (wl_span_t){NULL, 0U};
  }
  backend = rx_state(state);
  return (wl_span_t){
      (uint8_t *)lwrb_get_linear_block_read_address(&backend->buffer),
      (size_t)lwrb_get_linear_block_read_length(&backend->buffer)};
}

int wl_rx_ring_consumer_find(const wl_rx_ring_state_t *state, uint8_t value,
                             size_t *out_offset) {
  const wl_rx_lwrb_state_t *backend;
  lwrb_sz_t found = 0U;

  if (state == NULL || out_offset == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  backend = rx_state_const(state);
  if (lwrb_find(&backend->buffer, &value, 1U, 0U, &found) == 0U) {
    return WL_ERR_NO_DATA;
  }
  *out_offset = (size_t)found;
  return WL_OK;
}

int wl_rx_ring_consumer_copy(const wl_rx_ring_state_t *state, size_t offset,
                             uint8_t *output, size_t length) {
  const wl_rx_lwrb_state_t *backend;
  size_t readable;

  if (state == NULL || (output == NULL && length != 0U)) {
    return WL_ERR_INVALID_ARG;
  }
  backend = rx_state_const(state);
  readable = (size_t)lwrb_get_full(&backend->buffer);
  if (offset > readable || length > readable - offset) {
    return WL_ERR_NO_DATA;
  }
  if (length == 0U) {
    return WL_OK;
  }
  return (size_t)lwrb_peek(&backend->buffer, (lwrb_sz_t)offset, output,
                           (lwrb_sz_t)length) == length
             ? WL_OK
             : WL_ERR_INVALID_STATE;
}

int wl_rx_ring_consumer_consume(wl_rx_ring_state_t *state, size_t length) {
  wl_rx_lwrb_state_t *backend;
  size_t readable;

  if (state == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  backend = rx_state(state);
  readable = (size_t)lwrb_get_full(&backend->buffer);
  if (length > readable) {
    return WL_ERR_NO_DATA;
  }
  if (length == 0U) {
    return WL_OK;
  }
  return (size_t)lwrb_skip(&backend->buffer, (lwrb_sz_t)length) == length
             ? WL_OK
             : WL_ERR_INVALID_STATE;
}

unsigned int
wl_rx_ring_consumer_take_overflow(wl_rx_ring_state_t *state) {
  if (state == NULL) {
    return 0U;
  }
  return atomic_exchange_explicit(&rx_state(state)->overflow_events, 0U,
                                  memory_order_acq_rel);
}
