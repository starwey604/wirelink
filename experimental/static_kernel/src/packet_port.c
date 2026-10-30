/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_port.h"

#include <stdbool.h>
#include <string.h>

#define TX_ACTIVE 1U
#define TX_FLAGS 14U
#define TX_SHIFT 4U

wl_err_t wl_packet_init(wl_packet_port_t *port,
                        const wl_packet_config_t *config) {
  if (port == NULL || config == NULL || config->instance == 0U ||
      config->rx_storage == NULL || config->rx_unit_size == 0U ||
      config->rx_unit_size > SIZE_MAX / WL_PACKET_RX_SLOTS ||
      config->rx_storage_size < config->rx_unit_size * WL_PACKET_RX_SLOTS ||
      config->submit == NULL) return WL_ERR_INVALID_ARG;
  const wl_packet_config_t value = *config;
  memset(port, 0, sizeof(*port));
  port->config = value;
  atomic_init(&port->write_cursor, 0U);
  atomic_init(&port->read_cursor, 0U);
  atomic_init(&port->tx_state, 0U);
  atomic_init(&port->running, false);
  atomic_init(&port->claim_active, false);
  atomic_init(&port->activity, false);
  if (!atomic_is_lock_free(&port->write_cursor) ||
      !atomic_is_lock_free(&port->read_cursor) ||
      !atomic_is_lock_free(&port->tx_state) ||
      !atomic_is_lock_free(&port->running) ||
      !atomic_is_lock_free(&port->claim_active) ||
      !atomic_is_lock_free(&port->activity)) return WL_ERR_NOT_SUPPORTED;
  atomic_store_explicit(&port->running, true, memory_order_release);
  return WL_OK;
}

void wl_packet_activity(wl_packet_port_t *port) {
  atomic_store_explicit(&port->activity, true, memory_order_release);
  if (port->config.notify != NULL) port->config.notify(port->config.user);
}

int wl_packet_take_activity(wl_packet_port_t *port) {
  return atomic_exchange_explicit(&port->activity, false, memory_order_acquire);
}

wl_err_t wl_packet_rx_claim(wl_packet_port_t *port, size_t maximum,
                            wl_packet_claim_t *out) {
  if (port == NULL || out == NULL || maximum == 0U) return WL_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  if (!atomic_load_explicit(&port->running, memory_order_acquire))
    return WL_ERR_INVALID_STATE;
  if (atomic_load_explicit(&port->claim_active, memory_order_relaxed))
    return WL_ERR_BUSY;
  if (maximum > port->config.rx_unit_size) return WL_ERR_BUF_TOO_SMALL;
  uint32_t write = atomic_load_explicit(&port->write_cursor, memory_order_relaxed);
  uint32_t read = atomic_load_explicit(&port->read_cursor, memory_order_acquire);
  if ((uint32_t)(write - read) >= WL_PACKET_RX_SLOTS) return WL_ERR_WOULD_BLOCK;
  if (port->claim_sequence == UINT32_MAX) return WL_ERR_ID_EXHAUSTED;
  ++port->claim_sequence;
  port->claim_cursor = write;
  port->claim_capacity = maximum;
  out->span.data = port->config.rx_storage +
                   (write % WL_PACKET_RX_SLOTS) * port->config.rx_unit_size;
  out->span.length = maximum;
  out->token.instance = port->config.instance;
  out->token.sequence = port->claim_sequence;
  atomic_store_explicit(&port->claim_active, true, memory_order_release);
  return WL_OK;
}

static int claim_matches(const wl_packet_port_t *port,
                          const wl_packet_claim_t *claim) {
  return atomic_load_explicit(&port->claim_active, memory_order_acquire) &&
      claim->token.instance == port->config.instance &&
      claim->token.sequence == port->claim_sequence &&
      claim->span.data == port->config.rx_storage +
          (port->claim_cursor % WL_PACKET_RX_SLOTS) * port->config.rx_unit_size;
}

wl_err_t wl_packet_rx_commit(wl_packet_port_t *port,
                             const wl_packet_claim_t *claim, size_t length) {
  if (port == NULL || claim == NULL) return WL_ERR_INVALID_ARG;
  if (!claim_matches(port, claim)) return WL_ERR_NOT_FOUND;
  if (length == 0U || length > port->claim_capacity) return WL_ERR_INVALID_ARG;
  /* A completed read after stop is discarded; adapter still owes its stop ack. */
  if (!atomic_load_explicit(&port->running, memory_order_acquire)) {
    atomic_store_explicit(&port->claim_active, false, memory_order_release);
    wl_packet_activity(port);
    return WL_ERR_INVALID_STATE;
  }
  uint32_t write = port->claim_cursor;
  port->lengths[write % WL_PACKET_RX_SLOTS] = length;
  port->rx_sequences[write % WL_PACKET_RX_SLOTS] = port->claim_sequence;
  atomic_store_explicit(&port->write_cursor, write + 1U, memory_order_release);
  atomic_store_explicit(&port->claim_active, false, memory_order_release);
  wl_packet_activity(port);
  return WL_OK;
}

wl_err_t wl_packet_rx_abort(wl_packet_port_t *port,
                            const wl_packet_claim_t *claim) {
  if (port == NULL || claim == NULL) return WL_ERR_INVALID_ARG;
  if (!claim_matches(port, claim)) return WL_ERR_NOT_FOUND;
  atomic_store_explicit(&port->claim_active, false, memory_order_release);
  wl_packet_activity(port);
  return WL_OK;
}

int wl_packet_rx_ready(const wl_packet_port_t *port) {
  uint32_t read = atomic_load_explicit(&port->read_cursor, memory_order_relaxed);
  return read != atomic_load_explicit(&port->write_cursor, memory_order_acquire);
}

wl_err_t wl_packet_rx_borrow(wl_packet_port_t *port, wl_packet_borrow_t *out) {
  if (port == NULL || out == NULL) return WL_ERR_INVALID_ARG;
  if (port->borrowed) return WL_ERR_BUSY;
  if (!wl_packet_rx_ready(port)) return WL_ERR_NO_DATA;
  uint32_t read = atomic_load_explicit(&port->read_cursor, memory_order_relaxed);
  *out = (wl_packet_borrow_t){
      .data = port->config.rx_storage +
              (read % WL_PACKET_RX_SLOTS) * port->config.rx_unit_size,
      .length = port->lengths[read % WL_PACKET_RX_SLOTS],
      .token = {port->config.instance, port->rx_sequences[read % WL_PACKET_RX_SLOTS]}
  };
  port->borrowed = 1U;
  return WL_OK;
}

wl_err_t wl_packet_rx_release(wl_packet_port_t *port,
                              const wl_packet_borrow_t *borrow) {
  if (port == NULL || borrow == NULL) return WL_ERR_INVALID_ARG;
  uint32_t read = atomic_load_explicit(&port->read_cursor, memory_order_relaxed);
  if (!port->borrowed || borrow->token.instance != port->config.instance ||
      borrow->token.sequence != port->rx_sequences[read % WL_PACKET_RX_SLOTS] ||
      borrow->data != port->config.rx_storage +
          (read % WL_PACKET_RX_SLOTS) * port->config.rx_unit_size)
    return WL_ERR_NOT_FOUND;
  port->borrowed = 0U;
  atomic_store_explicit(&port->read_cursor, read + 1U, memory_order_release);
  wl_packet_activity(port);
  return WL_OK;
}

int wl_packet_tx_active(const wl_packet_port_t *port) {
  return (atomic_load_explicit(&port->tx_state, memory_order_acquire) & TX_ACTIVE) != 0U;
}

wl_err_t wl_packet_tx_submit(wl_packet_port_t *port, const uint8_t *data,
                             size_t length, wl_packet_token_t *out) {
  if (port == NULL || data == NULL || length == 0U || out == NULL)
    return WL_ERR_INVALID_ARG;
  if (!atomic_load_explicit(&port->running, memory_order_acquire))
    return WL_ERR_INVALID_STATE;
  if (wl_packet_tx_active(port)) return WL_ERR_WOULD_BLOCK;
  if (port->tx_sequence == WL_PACKET_SEQUENCE_MAX) return WL_ERR_ID_EXHAUSTED;
  ++port->tx_sequence;
  wl_packet_token_t token = {port->config.instance, port->tx_sequence};
  port->tx_delivered = 0U;
  /* Publish before submit: the sink is permitted to complete synchronously. */
  atomic_store_explicit(&port->tx_state,
                        (token.sequence << TX_SHIFT) | TX_ACTIVE,
                        memory_order_release);
  wl_packet_submit_t result = port->config.submit(port->config.user, data, length, token);
  if (result == WL_PACKET_BUSY || result == WL_PACKET_FAILED) {
    atomic_store_explicit(&port->tx_state, token.sequence << TX_SHIFT,
                          memory_order_release);
    return result == WL_PACKET_BUSY ? WL_ERR_WOULD_BLOCK : WL_ERR_IO;
  }
  *out = token;
  if (result == WL_PACKET_SENT)
    (void)wl_packet_tx_publish(port, token, WL_PACKET_DRAINED);
  return WL_OK;
}

wl_err_t wl_packet_tx_publish(wl_packet_port_t *port,
                              wl_packet_token_t token, unsigned flags) {
  if (port == NULL || flags == 0U || (flags & ~TX_FLAGS) != 0U)
    return WL_ERR_INVALID_ARG;
  if (token.instance != port->config.instance) return WL_ERR_NOT_FOUND;
  if ((flags & WL_PACKET_TX_ERROR) != 0U) flags |= WL_PACKET_DRAINED;
  if ((flags & WL_PACKET_DRAINED) != 0U) flags |= WL_PACKET_CONSUMED;
  uint_least32_t state = atomic_load_explicit(&port->tx_state, memory_order_acquire);
  for (;;) {
    if ((state >> TX_SHIFT) != token.sequence || (state & TX_ACTIVE) == 0U ||
        (state & WL_PACKET_DRAINED) != 0U ||
        (state & flags) == flags) return WL_ERR_NOT_FOUND;
    if (atomic_compare_exchange_weak_explicit(&port->tx_state, &state, state | flags,
                                              memory_order_acq_rel, memory_order_acquire))
      break;
  }
  wl_packet_activity(port);
  return WL_OK;
}

unsigned wl_packet_tx_take(wl_packet_port_t *port, wl_packet_token_t *out) {
  uint_least32_t state = atomic_load_explicit(&port->tx_state, memory_order_acquire);
  if ((state & TX_ACTIVE) == 0U) return 0U;
  unsigned flags = (unsigned)(state & TX_FLAGS) & ~port->tx_delivered;
  if (flags == 0U) return 0U;
  *out = (wl_packet_token_t){port->config.instance, (uint32_t)(state >> TX_SHIFT)};
  port->tx_delivered |= flags;
  if ((flags & WL_PACKET_DRAINED) != 0U)
    atomic_fetch_and_explicit(&port->tx_state, ~((uint_least32_t)TX_ACTIVE),
                              memory_order_acq_rel);
  return flags;
}

void wl_packet_stop(wl_packet_port_t *port) {
  atomic_store_explicit(&port->running, false, memory_order_release);
  wl_packet_activity(port);
}

int wl_packet_quiescent(const wl_packet_port_t *port) {
  return !atomic_load_explicit(&port->running, memory_order_acquire) &&
      !atomic_load_explicit(&port->claim_active, memory_order_acquire) &&
      !port->borrowed && !wl_packet_rx_ready(port) && !wl_packet_tx_active(port);
}
