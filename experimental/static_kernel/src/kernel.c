/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/kernel.h"

#include <limits.h>
#include <string.h>

enum { FREE, BUILDING, READY, LEASED };
enum { CALL_FREE, CALL_WAITING, CALL_TERMINAL };
#define NONE UINT8_MAX
#define REPLY_FIRST WL_KERNEL_CALLS
#define MESSAGE_TX (WL_KERNEL_TX_COUNT - 1U)

static uint64_t instance(const wl_kernel_t *kernel) {
  return kernel->port->config.instance;
}

static int expired(uint32_t now, uint32_t deadline) {
  return (uint32_t)(now - deadline) <= INT32_MAX;
}

static int free_tx(const wl_kernel_t *kernel, unsigned first, unsigned end) {
  for (unsigned i = first; i < end; ++i)
    if (kernel->tx[i].state == FREE) return (int)i;
  return -1;
}

wl_err_t wl_kernel_init(wl_kernel_t *kernel, const wl_kernel_config_t *config) {
  if (kernel == NULL || config == NULL || config->port == NULL ||
      config->route == NULL || config->port->config.instance == 0U)
    return WL_ERR_INVALID_ARG;
  for (unsigned i = 0U; i < WL_KERNEL_TX_COUNT; ++i)
    if (config->tx_storage[i].data == NULL || config->tx_storage[i].length <
        (i < WL_KERNEL_CALLS ? 17U : (i < MESSAGE_TX ? 21U : WL_KERNEL_NATIVE_HEADER)))
      return WL_ERR_INVALID_ARG;
  const wl_kernel_config_t value = *config;
  memset(kernel, 0, sizeof(*kernel));
  kernel->port = value.port;
  kernel->route = value.route;
  kernel->on_error = value.on_error;
  kernel->user = value.user;
  kernel->flight = NONE;
  for (unsigned i = 0U; i < WL_KERNEL_TX_COUNT; ++i)
    kernel->tx[i].storage = value.tx_storage[i];
  return WL_OK;
}

static wl_err_t encode_frame(wl_kernel_tx_t *tx, uint16_t id, size_t prefix,
                              const void *value, wl_kernel_encode_fn encode) {
  if (prefix > tx->storage.length) return WL_ERR_BUF_TOO_SMALL;
  size_t length = 0U;
  wl_err_t error = encode(value, tx->storage.data + prefix,
                           tx->storage.length - prefix, &length);
  if (error != WL_OK) return error;
  if (length > tx->storage.length - prefix) return WL_ERR_BUF_TOO_SMALL;
  error = wl_kernel_native_none_seal(tx->storage.data, tx->storage.length, id,
                                     prefix - WL_KERNEL_NATIVE_HEADER + length);
  if (error == WL_OK) {
    tx->message_id = id;
    tx->length = prefix + length;
    tx->state = READY;
  }
  return error;
}

wl_err_t wl_kernel_call_start(wl_kernel_t *kernel, uint16_t request_id,
                              uint16_t response_id, const void *value,
                              wl_kernel_encode_fn encode, uint32_t now, uint32_t timeout,
                              wl_kernel_complete_fn complete, void (*callback)(void),
                              void *user, wl_kernel_call_handle_t *out) {
  if (kernel == NULL || out == NULL || request_id == 0U || response_id == 0U ||
      request_id == response_id || encode == NULL || complete == NULL ||
      callback == NULL || timeout == 0U || timeout > INT32_MAX)
    return WL_ERR_INVALID_ARG;
  *out = (wl_kernel_call_handle_t){0};
  if (kernel->closed) return WL_ERR_INVALID_STATE;
  int slot = -1;
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i)
    if (kernel->calls[i].state == CALL_FREE && kernel->calls[i].generation != UINT32_MAX) {
      slot = (int)i;
      break;
    }
  if (kernel->operation == UINT32_MAX) return WL_ERR_ID_EXHAUSTED;
  if (slot < 0) {
    for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i)
      if (kernel->calls[i].state == CALL_FREE) return WL_ERR_ID_EXHAUSTED;
    return WL_ERR_WOULD_BLOCK;
  }
  int tx_index = free_tx(kernel, 0U, WL_KERNEL_CALLS);
  if (tx_index < 0) return WL_ERR_WOULD_BLOCK;
  wl_kernel_tx_t *tx = &kernel->tx[tx_index];
  uint32_t operation = kernel->operation + 1U;
  size_t header = 0U;
  wl_err_t error = wl_kernel_rpc_write(tx->storage.data + WL_KERNEL_NATIVE_HEADER,
      tx->storage.length - WL_KERNEL_NATIVE_HEADER, instance(kernel), operation, 0, &header);
  if (error == WL_OK)
    error = encode_frame(tx, request_id, WL_KERNEL_NATIVE_HEADER + header, value, encode);
  if (error != WL_OK) return error;
  wl_kernel_call_t *call = &kernel->calls[slot];
  ++call->generation;
  call->operation = operation;
  call->response_id = response_id;
  call->deadline = now + timeout;
  call->complete = complete;
  call->callback = callback;
  call->user = user;
  call->sent = 0U;
  call->state = CALL_WAITING;
  tx->call_slot = (uint8_t)slot;
  tx->generation = call->generation;
  kernel->operation = operation;
  *out = (wl_kernel_call_handle_t){instance(kernel), call->generation, operation, (uint8_t)slot};
  return WL_OK;
}

wl_err_t wl_kernel_cancel(wl_kernel_t *kernel, wl_kernel_call_handle_t handle) {
  if (kernel == NULL) return WL_ERR_INVALID_ARG;
  if (handle.instance != instance(kernel) || handle.slot >= WL_KERNEL_CALLS)
    return WL_ERR_NOT_FOUND;
  wl_kernel_call_t *call = &kernel->calls[handle.slot];
  if (call->state != CALL_WAITING || call->generation != handle.generation ||
      call->operation != handle.operation) return WL_ERR_NOT_FOUND;
  call->state = CALL_TERMINAL;
  call->result = (wl_kernel_result_t){WL_KERNEL_CANCELLED, WL_ERR_CANCELLED};
  return WL_OK;
}

wl_err_t wl_kernel_send(wl_kernel_t *kernel, uint16_t id,
                         const void *value, wl_kernel_encode_fn encode) {
  if (kernel == NULL || id == 0U || encode == NULL) return WL_ERR_INVALID_ARG;
  if (kernel->closed) return WL_ERR_INVALID_STATE;
  wl_kernel_tx_t *tx = &kernel->tx[MESSAGE_TX];
  if (tx->state != FREE) return WL_ERR_WOULD_BLOCK;
  return encode_frame(tx, id, WL_KERNEL_NATIVE_HEADER, value, encode);
}

wl_err_t wl_kernel_reply_reserve(wl_kernel_t *kernel, unsigned *out) {
  if (kernel == NULL || out == NULL || !kernel->in_step || kernel->closed)
    return WL_ERR_INVALID_STATE;
  int index = free_tx(kernel, REPLY_FIRST, MESSAGE_TX);
  if (index < 0) return WL_ERR_WOULD_BLOCK;
  kernel->tx[index].state = BUILDING;
  *out = (unsigned)index;
  return WL_OK;
}

void wl_kernel_reply_abort(wl_kernel_t *kernel, unsigned index) {
  if (index >= REPLY_FIRST && index < MESSAGE_TX && kernel->tx[index].state == BUILDING)
    kernel->tx[index].state = FREE;
}

wl_err_t wl_kernel_reply_finish(wl_kernel_t *kernel, unsigned index, uint16_t id,
                                const wl_kernel_rpc_metadata_t *request, int32_t status,
                                const void *value, wl_kernel_encode_fn encode) {
  if (kernel == NULL || request == NULL || id == 0U || !kernel->in_step ||
      index < REPLY_FIRST || index >= MESSAGE_TX ||
      kernel->tx[index].state != BUILDING || (status == 0 && encode == NULL))
    return WL_ERR_INVALID_ARG;
  wl_kernel_tx_t *tx = &kernel->tx[index];
  size_t header = 0U;
  wl_err_t error = wl_kernel_rpc_write(tx->storage.data + WL_KERNEL_NATIVE_HEADER,
      tx->storage.length - WL_KERNEL_NATIVE_HEADER, request->session,
      request->operation, status, &header);
  if (error == WL_OK && status == 0)
    error = encode_frame(tx, id, WL_KERNEL_NATIVE_HEADER + header, value, encode);
  else if (error == WL_OK) {
    error = wl_kernel_native_none_seal(tx->storage.data, tx->storage.length, id, header);
    if (error == WL_OK) {
      tx->message_id = id;
      tx->length = WL_KERNEL_NATIVE_HEADER + header;
      tx->state = READY;
    }
  }
  if (error != WL_OK) tx->state = FREE;
  return error;
}

static void finish_call(wl_kernel_t *kernel, unsigned slot, wl_kernel_result_t result,
                         const uint8_t *body, size_t length) {
  wl_kernel_call_t saved = kernel->calls[slot];
  /* Release logical ownership before callback so it may create another call.
   * Physical LEASED bytes keep their separate generation until DRAINED. */
  kernel->calls[slot].state = CALL_FREE;
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i)
    if (kernel->tx[i].state == READY && kernel->tx[i].call_slot == slot &&
        kernel->tx[i].generation == saved.generation) kernel->tx[i].state = FREE;
  saved.complete(kernel, &saved, result, body, length);
}

wl_err_t wl_kernel_response(wl_kernel_t *kernel, uint16_t id,
                             const uint8_t *data, size_t length) {
  if (!kernel->in_step) return WL_ERR_INVALID_STATE;
  wl_kernel_rpc_metadata_t metadata;
  wl_err_t error = wl_kernel_rpc_read(data, length, 0, &metadata);
  if (error != WL_OK) return error;
  if (metadata.session != instance(kernel)) return WL_OK;
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i) {
    wl_kernel_call_t *call = &kernel->calls[i];
    if (call->state != CALL_WAITING || !call->sent || call->operation != metadata.operation ||
        call->response_id != id) continue;
    wl_kernel_result_t result = {
        metadata.status == 0 ? WL_KERNEL_SUCCESS : WL_KERNEL_REJECTED, metadata.status};
    finish_call(kernel, i, result, metadata.body, metadata.length);
    break;
  }
  return WL_OK;
}

static wl_err_t report(wl_kernel_t *kernel, uint16_t id, wl_err_t error) {
  if (error != WL_OK && kernel->on_error != NULL)
    kernel->on_error(kernel->user, id, error);
  return error;
}

static wl_err_t complete_tx(wl_kernel_t *kernel) {
  wl_packet_token_t token;
  unsigned flags = wl_packet_tx_take(kernel->port, &token);
  if (flags == 0U || kernel->flight == NONE) return WL_OK;
  if (token.instance != kernel->flight_token.instance ||
      token.sequence != kernel->flight_token.sequence) return WL_ERR_INVALID_STATE;
  wl_kernel_tx_t *tx = &kernel->tx[kernel->flight];
  wl_err_t error = WL_OK;
  if ((flags & WL_PACKET_TX_ERROR) != 0U) {
    if (kernel->flight < WL_KERNEL_CALLS) {
      wl_kernel_call_t *call = &kernel->calls[tx->call_slot];
      if (call->state == CALL_WAITING && call->generation == tx->generation) {
        call->state = CALL_TERMINAL;
        call->result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, WL_ERR_IO};
      }
    }
    error = report(kernel, tx->message_id, WL_ERR_IO);
  }
  if ((flags & WL_PACKET_DRAINED) != 0U) {
    tx->state = FREE;
    kernel->flight = NONE;
    kernel->tx_blocked = 0U;
  }
  return error;
}

static int terminal(wl_kernel_t *kernel, uint32_t now, const uint32_t *generations) {
  for (unsigned n = 0U; n < WL_KERNEL_CALLS; ++n) {
    unsigned slot = kernel->call_cursor++ % WL_KERNEL_CALLS;
    wl_kernel_call_t *call = &kernel->calls[slot];
    if (call->generation != generations[slot] || call->state == CALL_FREE) continue;
    if (call->state == CALL_WAITING && !expired(now, call->deadline)) continue;
    wl_kernel_result_t result = call->state == CALL_TERMINAL ? call->result :
        (wl_kernel_result_t){WL_KERNEL_TIMED_OUT, WL_ERR_TIMEOUT};
    finish_call(kernel, slot, result, NULL, 0U);
    return 1;
  }
  return 0;
}

static int submit_tx(wl_kernel_t *kernel, wl_err_t *out_error) {
  if (kernel->closed || kernel->tx_blocked || wl_packet_tx_active(kernel->port))
    return 0;
  for (unsigned n = 0U; n < WL_KERNEL_TX_COUNT; ++n) {
    unsigned index = kernel->tx_cursor;
    kernel->tx_cursor = (uint8_t)((index + 1U) % WL_KERNEL_TX_COUNT);
    wl_kernel_tx_t *tx = &kernel->tx[index];
    if (tx->state != READY) continue;
    if (index < WL_KERNEL_CALLS &&
        (kernel->calls[tx->call_slot].state != CALL_WAITING ||
         kernel->calls[tx->call_slot].generation != tx->generation)) {
      tx->state = FREE;
      continue;
    }
    wl_err_t error = wl_packet_tx_submit(kernel->port, tx->storage.data,
                                          tx->length, &kernel->flight_token);
    if (error == WL_ERR_WOULD_BLOCK) kernel->tx_blocked = 1U;
    else if (error == WL_OK) {
      kernel->flight = (uint8_t)index;
      tx->state = LEASED;
      if (index < WL_KERNEL_CALLS) {
        wl_kernel_call_t *call = &kernel->calls[tx->call_slot];
        if (call->state == CALL_WAITING && call->generation == tx->generation)
          call->sent = 1U;
      }
    } else {
      if (index < WL_KERNEL_CALLS) {
        wl_kernel_call_t *call = &kernel->calls[tx->call_slot];
        if (call->state == CALL_WAITING && call->generation == tx->generation) {
          call->state = CALL_TERMINAL;
          call->result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, error};
        }
      }
      tx->state = FREE;
      *out_error = report(kernel, tx->message_id, error);
    }
    return 1;
  }
  return 0;
}

static int receive(wl_kernel_t *kernel, wl_err_t *out_error) {
  if (kernel->rx_blocked && !kernel->closed &&
      free_tx(kernel, REPLY_FIRST, MESSAGE_TX) < 0) return 0;
  if (!kernel->rx_held) {
    if (wl_packet_rx_borrow(kernel->port, &kernel->rx) != WL_OK) return 0;
    kernel->rx_held = 1U;
  }
  wl_kernel_frame_t frame = {0};
  wl_err_t error = WL_OK;
  if (!kernel->closed) {
    error = wl_kernel_native_none_read(kernel->rx.data, kernel->rx.length, &frame);
    if (error == WL_OK) error = kernel->route(kernel, &frame);
  }
  if (error == WL_ERR_WOULD_BLOCK) {
    kernel->rx_blocked = 1U;
    return 1;
  }
  kernel->rx_blocked = 0U;
  kernel->rx_held = 0U;
  (void)wl_packet_rx_release(kernel->port, &kernel->rx);
  if (error != WL_OK) *out_error = report(kernel, frame.message_id, error);
  return 1;
}

wl_err_t wl_kernel_step(wl_kernel_t *kernel, uint32_t now, unsigned budget) {
  if (kernel == NULL || budget == 0U || budget > WL_KERNEL_BUDGET) return WL_ERR_INVALID_ARG;
  if (kernel->in_step) return WL_ERR_REENTRANT;
  kernel->in_step = 1U;
  uint32_t generations[WL_KERNEL_CALLS];
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i) generations[i] = kernel->calls[i].generation;
  if (wl_packet_take_activity(kernel->port)) kernel->tx_blocked = 0U;
  unsigned count = 0U;
  wl_err_t result = WL_OK;
  while (count < budget) {
    unsigned before = count;
    wl_err_t error = WL_OK;
    if (wl_packet_tx_ready(kernel->port)) { error = complete_tx(kernel); ++count; }
    if (count < budget && terminal(kernel, now, generations)) ++count;
    if (count < budget && submit_tx(kernel, &error)) ++count;
    if (count < budget && receive(kernel, &error)) ++count;
    if (result == WL_OK) result = error;
    if (before == count) break;
  }
  kernel->in_step = 0U;
  return result;
}

int wl_kernel_ready(const wl_kernel_t *kernel, uint32_t now,
                     int *has_deadline, uint32_t *deadline) {
  *has_deadline = 0;
  int ready = wl_packet_tx_ready(kernel->port);
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i) {
    const wl_kernel_call_t *call = &kernel->calls[i];
    if (call->state == CALL_FREE) continue;
    if (call->state == CALL_TERMINAL || expired(now, call->deadline)) ready = 1;
    else if (!*has_deadline || (uint32_t)(call->deadline - now) < (uint32_t)(*deadline - now)) {
      *has_deadline = 1;
      *deadline = call->deadline;
    }
  }
  if (wl_packet_rx_ready(kernel->port) &&
      (!kernel->rx_blocked || kernel->closed || free_tx(kernel, REPLY_FIRST, MESSAGE_TX) >= 0))
    ready = 1;
  if (!kernel->closed && !wl_packet_tx_active(kernel->port) &&
      (!kernel->tx_blocked || wl_packet_activity_pending(kernel->port)))
    for (unsigned i = 0U; i < WL_KERNEL_TX_COUNT; ++i)
      if (kernel->tx[i].state == READY) ready = 1;
  return ready;
}

wl_err_t wl_kernel_close(wl_kernel_t *kernel) {
  if (kernel == NULL) return WL_ERR_INVALID_ARG;
  if (kernel->in_step) return WL_ERR_REENTRANT;
  kernel->closed = 1U;
  wl_packet_stop(kernel->port);
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i)
    if (kernel->calls[i].state == CALL_WAITING) {
      kernel->calls[i].state = CALL_TERMINAL;
      kernel->calls[i].result = (wl_kernel_result_t){WL_KERNEL_CLOSED, WL_ERR_INVALID_STATE};
    }
  for (unsigned i = 0U; i < WL_KERNEL_TX_COUNT; ++i)
    if (kernel->tx[i].state != LEASED) kernel->tx[i].state = FREE;
  return WL_OK;
}

int wl_kernel_quiescent(const wl_kernel_t *kernel) {
  if (!kernel->closed || kernel->in_step || !wl_packet_quiescent(kernel->port)) return 0;
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i)
    if (kernel->calls[i].state != CALL_FREE) return 0;
  return 1;
}
