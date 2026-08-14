/* SPDX-License-Identifier: Apache-2.0 */

#include "rx_ring.h"

#include <string.h>

#include "wirelink/bipbuf.h"
#include "wirelink/types.h"

typedef struct {
  wl_bipbuf_t buffer;
  uint8_t reservation_active;
  uint8_t reservation_wrapped;
} wl_rx_bipbuf_state_t;

_Static_assert(sizeof(wl_rx_bipbuf_state_t) <= WL_RX_RING_STATE_SIZE,
               "WL_RX_RING_STATE_SIZE is too small for the BipBuffer backend");

static wl_rx_bipbuf_state_t *rx_state(wl_rx_ring_state_t *state) {
  return (wl_rx_bipbuf_state_t *)(void *)state->bytes;
}

static const wl_rx_bipbuf_state_t *rx_state_const(
    const wl_rx_ring_state_t *state) {
  return (const wl_rx_bipbuf_state_t *)(const void *)state->bytes;
}

size_t wl_rx_ring_storage_size(size_t usable_capacity) {
  return usable_capacity;
}

int wl_rx_ring_init(wl_rx_ring_state_t *state, uint8_t *memory,
                    size_t memory_size) {
  wl_rx_bipbuf_state_t *backend;

  if (state == NULL || memory == NULL || memory_size == 0U) {
    return WL_ERR_INVALID_ARG;
  }

  memset(state, 0, sizeof(*state));
  backend = rx_state(state);
  return wl_bb_init(&backend->buffer, memory, memory_size);
}

int wl_rx_ring_producer_reserve(wl_rx_ring_state_t *state,
                                wl_span_t *out_span) {
  wl_rx_bipbuf_state_t *backend;
  wl_span_t tail;
  wl_span_t wrapped;

  if (state == NULL || out_span == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  backend = rx_state(state);
  if (backend->reservation_active != 0U) {
    return WL_ERR_INVALID_STATE;
  }

  tail = wl_bb_get_write_buf(&backend->buffer);
  wrapped = wl_bb_get_write_buf_force_wrap(&backend->buffer);
  if (wrapped.length > tail.length) {
    *out_span = wrapped;
    backend->reservation_wrapped = 1U;
  } else {
    *out_span = tail;
    backend->reservation_wrapped = 0U;
  }
  backend->reservation_active = 1U;
  return WL_OK;
}

int wl_rx_ring_producer_commit(wl_rx_ring_state_t *state, size_t length) {
  wl_rx_bipbuf_state_t *backend;
  bool advanced;

  if (state == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  backend = rx_state(state);
  if (backend->reservation_active == 0U) {
    return WL_ERR_INVALID_STATE;
  }

  if (backend->reservation_wrapped != 0U) {
    advanced = wl_bb_advance_write_index_wrapped(&backend->buffer, length);
  } else {
    advanced = wl_bb_advance_write_index(&backend->buffer, length);
  }
  backend->reservation_active = 0U;
  backend->reservation_wrapped = 0U;
  return advanced ? WL_OK : WL_ERR_INVALID_ARG;
}

size_t wl_rx_ring_readable(const wl_rx_ring_state_t *state) {
  if (state == NULL) {
    return 0U;
  }
  return wl_bb_readable_bytes(&rx_state_const(state)->buffer);
}

wl_span_t wl_rx_ring_consumer_peek(wl_rx_ring_state_t *state) {
  if (state == NULL) {
    return (wl_span_t){NULL, 0U};
  }
  return wl_bb_get_contiguous_read_buf(&rx_state(state)->buffer);
}

int wl_rx_ring_consumer_find(const wl_rx_ring_state_t *state, uint8_t value,
                             size_t *out_offset) {
  const wl_rx_bipbuf_state_t *backend;
  size_t readable;
  uint8_t byte;

  if (state == NULL || out_offset == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  backend = rx_state_const(state);
  readable = wl_bb_readable_bytes(&backend->buffer);
  for (size_t i = 0U; i < readable; ++i) {
    if (wl_bb_peek((wl_bipbuf_t *)&backend->buffer, &byte, i, 1U) != WL_OK) {
      return WL_ERR_INVALID_STATE;
    }
    if (byte == value) {
      *out_offset = i;
      return WL_OK;
    }
  }
  return WL_ERR_NO_DATA;
}

int wl_rx_ring_consumer_copy(const wl_rx_ring_state_t *state, size_t offset,
                             uint8_t *output, size_t length) {
  const wl_rx_bipbuf_state_t *backend;

  if (state == NULL || (output == NULL && length != 0U)) {
    return WL_ERR_INVALID_ARG;
  }
  backend = rx_state_const(state);
  return wl_bb_peek((wl_bipbuf_t *)&backend->buffer, output, offset, length);
}

int wl_rx_ring_consumer_consume(wl_rx_ring_state_t *state, size_t length) {
  if (state == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  return wl_bb_discard(&rx_state(state)->buffer, length);
}
