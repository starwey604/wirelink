/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/kernel.h"

#include <limits.h>
#include <string.h>

enum { FREE, BUILDING, READY, LEASED };
enum { CALL_FREE, CALL_WAITING, CALL_TERMINAL };
#define NONE UINT8_MAX
#define REPLY_FIRST WL_KERNEL_CALLS
#define MESSAGE_TX (WL_KERNEL_TX_COUNT - 1U)
#define CALL_PREFIX (WL_KERNEL_NATIVE_HEADER + WL_KERNEL_RPC_HEADER_DATA)

static uint32_t instance(const wl_kernel_t *kernel) {
  return kernel->port->config.instance;
}

static int expired(uint32_t now, uint32_t deadline) {
  return (uint32_t)(now - deadline) <= INT32_MAX;
}

static uint8_t *tx_buf(const wl_kernel_t *kernel, unsigned index) {
  if (index < WL_KERNEL_CALLS)
    return kernel->storage.requests + index * (size_t)kernel->storage.request_stride;
  if (index < MESSAGE_TX)
    return kernel->storage.replies + (index - WL_KERNEL_CALLS) * (size_t)kernel->storage.reply_stride;
  return kernel->storage.message;
}

static int free_tx(const wl_kernel_t *kernel, unsigned first, unsigned end) {
  for (unsigned i = first; i < end; ++i)
    if (kernel->tx[i].state == FREE) return (int)i;
  return -1;
}

/* Admission order across requests, replies and ordinary messages is observable
 * by applications (e.g. Progress followed by Control STOP). Eight TX slots let
 * every ring calculation collapse into a single bitmask instruction. */
static void queue_push(wl_kernel_t *kernel, unsigned index) {
  unsigned tail = (kernel->tx_head + kernel->tx_count) & WL_KERNEL_TX_MASK;
  kernel->tx_queue[tail] = (uint8_t)index;
  ++kernel->tx_count;
}

static void queue_pop(wl_kernel_t *kernel) {
  kernel->tx_head = (uint8_t)((kernel->tx_head + 1U) & WL_KERNEL_TX_MASK);
  --kernel->tx_count;
}

static void queue_remove(wl_kernel_t *kernel, unsigned index) {
  unsigned kept = 0U, count = kernel->tx_count;
  for (unsigned n = 0U; n < count; ++n) {
    uint8_t value = kernel->tx_queue[(kernel->tx_head + n) & WL_KERNEL_TX_MASK];
    if (value != index) {
      kernel->tx_queue[(kernel->tx_head + kept) & WL_KERNEL_TX_MASK] = value;
      ++kept;
    }
  }
  kernel->tx_count = (uint8_t)kept;
}

static wl_err_t seal_and_queue(wl_kernel_t *kernel, unsigned index, uint8_t *frame,
                               size_t capacity, uint8_t kind, uint16_t id,
                               size_t wire_payload) {
  wl_err_t error = wl_wire_frame_seal(frame, capacity, kind, id, wire_payload);
  if (error != WL_OK) return error;
  wl_kernel_tx_t *tx = &kernel->tx[index];
  tx->message_id = id;
  tx->length = (uint16_t)(WL_KERNEL_NATIVE_HEADER + wire_payload);
  tx->state = READY;
  queue_push(kernel, index);
  return WL_OK;
}

static void fail_call(wl_kernel_call_t *call, wl_kernel_outcome_t outcome,
                      int32_t status) {
  if (call->state == CALL_WAITING) {
    call->state = CALL_TERMINAL;
    call->result = (wl_kernel_result_t){outcome, status};
  }
}

static void fail_tx_call(wl_kernel_t *kernel, unsigned index, wl_err_t status) {
  if (index < WL_KERNEL_CALLS) {
    const wl_kernel_tx_t *tx = &kernel->tx[index];
    wl_kernel_call_t *call = &kernel->calls[tx->call_slot];
    if (call->generation == tx->generation)
      fail_call(call, WL_KERNEL_LOCAL_ERROR, status);
  }
}

static wl_err_t find_call_slot(const wl_kernel_t *kernel, int *out_slot) {
  if (kernel->operation == UINT32_MAX) return WL_ERR_ID_EXHAUSTED;
  int exhausted = 0;
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i) {
    if (kernel->calls[i].state == CALL_FREE) {
      if (kernel->calls[i].generation != UINT32_MAX) {
        *out_slot = (int)i;
        return WL_OK;
      }
      exhausted = 1;
    }
  }
  return exhausted ? WL_ERR_ID_EXHAUSTED : WL_ERR_WOULD_BLOCK;
}

wl_err_t wl_kernel_init(wl_kernel_t *kernel, const wl_kernel_config_t *config) {
  if (kernel == NULL || config == NULL || config->port == NULL ||
      config->route == NULL || config->port->config.instance == 0U ||
      config->storage.requests == NULL ||
      config->storage.request_stride < CALL_PREFIX ||
      config->storage.replies == NULL ||
      config->storage.reply_stride < (WL_KERNEL_NATIVE_HEADER + WL_KERNEL_RPC_HEADER_ERR) ||
      config->storage.message == NULL ||
      config->storage.message_capacity < WL_KERNEL_NATIVE_HEADER)
    return WL_ERR_INVALID_ARG;
  const wl_kernel_config_t value = *config;
  memset(kernel, 0, sizeof(*kernel));
  kernel->port = value.port;
  kernel->storage = value.storage;
  kernel->route = value.route;
  kernel->on_error = value.on_error;
  kernel->user = value.user;
  kernel->flight = NONE;
  return WL_OK;
}

WL_HOT_ATTR wl_err_t wl_kernel_msg_claim(wl_kernel_t *kernel,
                                         uint8_t **out_frame,
                                         size_t *out_capacity) {
  if (kernel == NULL || out_frame == NULL) return WL_ERR_INVALID_ARG;
  if (kernel->closed) return WL_ERR_INVALID_STATE;
  if (kernel->tx[MESSAGE_TX].state != FREE) return WL_ERR_WOULD_BLOCK;
  *out_frame = kernel->storage.message;
  if (out_capacity != NULL) *out_capacity = kernel->storage.message_capacity;
  return WL_OK;
}

WL_HOT_ATTR wl_err_t wl_kernel_msg_commit(wl_kernel_t *kernel,
                                          size_t frame_length) {
  if (kernel == NULL || frame_length < WL_KERNEL_NATIVE_HEADER)
    return WL_ERR_INVALID_ARG;
  if (kernel->closed) return WL_ERR_INVALID_STATE;
  if (kernel->tx[MESSAGE_TX].state != FREE) return WL_ERR_WOULD_BLOCK;
  if (frame_length - WL_KERNEL_NATIVE_HEADER > WL_WIRE_MAX_PAYLOAD)
    return WL_ERR_PAYLOAD_TOO_LONG;
  if (frame_length > kernel->storage.message_capacity)
    return WL_ERR_BUF_TOO_SMALL;
  uint8_t *frame = kernel->storage.message;
  uint16_t id = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8U);
  if (frame[0] != WL_WIRE_MAGIC || frame[1] != WL_WIRE_MSG || id == 0U)
    return WL_ERR_BAD_FRAME;
  wl_kernel_tx_t *tx = &kernel->tx[MESSAGE_TX];
  tx->message_id = id;
  tx->length = (uint16_t)frame_length;
  tx->state = READY;
  queue_push(kernel, MESSAGE_TX);
  return WL_OK;
}

WL_HOT_ATTR wl_err_t wl_kernel_call_claim(wl_kernel_t *kernel,
                                          uint8_t *out_slot,
                                          uint8_t **out_payload,
                                          size_t *out_capacity) {
  if (kernel == NULL || out_slot == NULL || out_payload == NULL)
    return WL_ERR_INVALID_ARG;
  if (kernel->closed) return WL_ERR_INVALID_STATE;
  int call_slot = -1;
  wl_err_t error = find_call_slot(kernel, &call_slot);
  if (error != WL_OK) return error;
  int tx_index = free_tx(kernel, 0U, WL_KERNEL_CALLS);
  if (tx_index < 0) return WL_ERR_WOULD_BLOCK;
  *out_slot = (uint8_t)tx_index;
  *out_payload = tx_buf(kernel, (unsigned)tx_index) + CALL_PREFIX;
  if (out_capacity != NULL)
    *out_capacity = (size_t)kernel->storage.request_stride - CALL_PREFIX;
  return WL_OK;
}

WL_HOT_ATTR wl_err_t wl_kernel_call_commit(
    wl_kernel_t *kernel, uint8_t slot, uint16_t request_id,
    uint16_t response_id, size_t body_length, uint32_t now, uint32_t timeout,
    wl_kernel_complete_fn complete, void (*callback)(void), void *user,
    wl_kernel_call_handle_t *out) {
  if (out != NULL) *out = (wl_kernel_call_handle_t){0};
  if (kernel == NULL || out == NULL || slot >= WL_KERNEL_CALLS ||
      request_id == 0U || response_id == 0U || request_id == response_id ||
      complete == NULL || callback == NULL || timeout == 0U ||
      timeout > INT32_MAX)
    return WL_ERR_INVALID_ARG;
  if (kernel->closed) return WL_ERR_INVALID_STATE;
  wl_kernel_tx_t *tx = &kernel->tx[slot];
  if (tx->state != FREE) return WL_ERR_INVALID_STATE;
  int call_slot = -1;
  wl_err_t error = find_call_slot(kernel, &call_slot);
  if (error != WL_OK) return error;
  if (body_length > WL_WIRE_MAX_PAYLOAD - WL_KERNEL_RPC_HEADER_DATA)
    return WL_ERR_PAYLOAD_TOO_LONG;
  uint8_t *frame = tx_buf(kernel, slot);
  error = seal_and_queue(kernel, slot, frame, kernel->storage.request_stride,
                         WL_WIRE_RPC_REQ, request_id,
                         WL_KERNEL_RPC_HEADER_DATA + body_length);
  if (error != WL_OK) return error;
  uint32_t operation = kernel->operation + 1U;
  wl_wire_put_u32(frame + WL_KERNEL_NATIVE_HEADER, instance(kernel));
  wl_wire_put_u32(frame + WL_KERNEL_NATIVE_HEADER + 4U, operation);
  wl_kernel_call_t *call = &kernel->calls[call_slot];
  ++call->generation;
  call->operation = operation;
  call->response_id = response_id;
  call->deadline = now + timeout;
  call->complete = complete;
  call->callback = callback;
  call->user = user;
  call->sent = 0U;
  call->state = CALL_WAITING;
  tx->call_slot = (uint8_t)call_slot;
  tx->generation = call->generation;
  kernel->operation = operation;
  *out = (wl_kernel_call_handle_t){
      instance(kernel), call->generation, operation, (uint8_t)call_slot};
  return WL_OK;
}

wl_err_t wl_kernel_cancel(wl_kernel_t *kernel, wl_kernel_call_handle_t handle) {
  if (kernel == NULL) return WL_ERR_INVALID_ARG;
  if (handle.instance != instance(kernel) || handle.slot >= WL_KERNEL_CALLS)
    return WL_ERR_NOT_FOUND;
  wl_kernel_call_t *call = &kernel->calls[handle.slot];
  if (call->state != CALL_WAITING || call->generation != handle.generation ||
      call->operation != handle.operation)
    return WL_ERR_NOT_FOUND;
  fail_call(call, WL_KERNEL_CANCELLED, WL_ERR_CANCELLED);
  return WL_OK;
}

WL_HOT_ATTR wl_err_t wl_kernel_reply_claim(
    wl_kernel_t *kernel, uint16_t response_id,
    const wl_kernel_rpc_metadata_t *request, uint8_t *out_slot,
    uint8_t **out_payload, size_t *out_capacity) {
  if (kernel == NULL || !kernel->in_step || kernel->closed)
    return WL_ERR_INVALID_STATE;
  if (response_id == 0U || request == NULL || request->session == 0U ||
      request->operation == 0U || out_slot == NULL || out_payload == NULL)
    return WL_ERR_INVALID_ARG;
  int index = free_tx(kernel, REPLY_FIRST, MESSAGE_TX);
  if (index < 0) return WL_ERR_WOULD_BLOCK;
  uint8_t *frame = tx_buf(kernel, (unsigned)index);
  kernel->tx[index].state = BUILDING;
  kernel->tx[index].message_id = response_id;
  wl_wire_put_u32(frame + WL_KERNEL_NATIVE_HEADER, request->session);
  wl_wire_put_u32(frame + WL_KERNEL_NATIVE_HEADER + 4U, request->operation);
  *out_slot = (uint8_t)index;
  *out_payload = frame + CALL_PREFIX;
  if (out_capacity != NULL)
    *out_capacity = (size_t)kernel->storage.reply_stride - CALL_PREFIX;
  return WL_OK;
}

WL_HOT_ATTR void wl_kernel_reply_abort(wl_kernel_t *kernel, uint8_t slot) {
  if (kernel != NULL && slot >= REPLY_FIRST && slot < MESSAGE_TX &&
      kernel->tx[slot].state == BUILDING)
    kernel->tx[slot].state = FREE;
}

static wl_err_t reply_finish(wl_kernel_t *kernel, uint8_t slot, uint8_t kind,
                             size_t wire_payload, int32_t status) {
  if (kernel == NULL || !kernel->in_step || slot < REPLY_FIRST ||
      slot >= MESSAGE_TX || kernel->tx[slot].state != BUILDING)
    return WL_ERR_INVALID_ARG;
  wl_kernel_tx_t *tx = &kernel->tx[slot];
  if (kind == WL_WIRE_RPC_ERR) {
    if (status == 0) {
      tx->state = FREE;
      return WL_ERR_INVALID_ARG;
    }
    wl_wire_put_u32(tx_buf(kernel, slot) + CALL_PREFIX, (uint32_t)status);
  }
  wl_err_t error = seal_and_queue(kernel, slot, tx_buf(kernel, slot),
                                  kernel->storage.reply_stride, kind,
                                  tx->message_id, wire_payload);
  if (error != WL_OK) tx->state = FREE;
  return error;
}

WL_HOT_ATTR wl_err_t wl_kernel_reply_commit(wl_kernel_t *kernel, uint8_t slot,
                                            size_t body_length) {
  if (body_length > WL_WIRE_MAX_PAYLOAD - WL_KERNEL_RPC_HEADER_DATA) {
    wl_kernel_reply_abort(kernel, slot);
    return WL_ERR_PAYLOAD_TOO_LONG;
  }
  return reply_finish(kernel, slot, WL_WIRE_RPC_RESP,
                      WL_KERNEL_RPC_HEADER_DATA + body_length, 0);
}

WL_HOT_ATTR wl_err_t wl_kernel_reply_error(wl_kernel_t *kernel, uint8_t slot,
                                           int32_t status) {
  return reply_finish(kernel, slot, WL_WIRE_RPC_ERR,
                      WL_KERNEL_RPC_HEADER_ERR, status);
}

static void finish_call(wl_kernel_t *kernel, unsigned slot,
                        wl_kernel_result_t result, const uint8_t *body,
                        size_t length) {
  wl_kernel_call_t saved = kernel->calls[slot];
  /* Release logical ownership before callback so it may create another call.
   * Physical LEASED bytes keep their separate generation until DRAINED. */
  kernel->calls[slot].state = CALL_FREE;
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i)
    if (kernel->tx[i].state == READY && kernel->tx[i].call_slot == slot &&
        kernel->tx[i].generation == saved.generation) {
      kernel->tx[i].state = FREE;
      queue_remove(kernel, i);
    }
  saved.complete(kernel, &saved, result, body, length);
}

WL_HOT_ATTR wl_err_t wl_kernel_response(wl_kernel_t *kernel,
                                        const wl_kernel_frame_t *frame) {
  if (kernel == NULL || frame == NULL) return WL_ERR_INVALID_ARG;
  if (!kernel->in_step) return WL_ERR_INVALID_STATE;
  if (frame->kind != WL_WIRE_RPC_RESP && frame->kind != WL_WIRE_RPC_ERR)
    return WL_ERR_CORRUPT_PAYLOAD;
  wl_kernel_rpc_metadata_t metadata;
  wl_err_t error =
      wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
  if (error != WL_OK) return error;
  if (metadata.session != instance(kernel)) return WL_OK;
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i) {
    wl_kernel_call_t *call = &kernel->calls[i];
    if (call->state != CALL_WAITING || !call->sent ||
        call->operation != metadata.operation ||
        call->response_id != frame->message_id)
      continue;
    wl_kernel_result_t result = {
        metadata.status == 0 ? WL_KERNEL_SUCCESS : WL_KERNEL_REJECTED,
        metadata.status};
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

WL_HOT_ATTR static wl_err_t complete_tx(wl_kernel_t *kernel) {
  wl_packet_token_t token;
  unsigned flags = wl_packet_tx_take(kernel->port, &token);
  if (flags == 0U || kernel->flight == NONE) return WL_OK;
  if (token.instance != kernel->flight_token.instance ||
      token.sequence != kernel->flight_token.sequence)
    return WL_ERR_INVALID_STATE;
  wl_kernel_tx_t *tx = &kernel->tx[kernel->flight];
  wl_err_t error = WL_OK;
  if ((flags & WL_PACKET_TX_ERROR) != 0U) {
    fail_tx_call(kernel, kernel->flight, WL_ERR_IO);
    error = report(kernel, tx->message_id, WL_ERR_IO);
  }
  if ((flags & WL_PACKET_DRAINED) != 0U) {
    tx->state = FREE;
    kernel->flight = NONE;
    kernel->tx_blocked = 0U;
  }
  return error;
}

WL_HOT_ATTR static int terminal(wl_kernel_t *kernel, uint32_t now,
                                const uint32_t *generations) {
  for (unsigned n = 0U; n < WL_KERNEL_CALLS; ++n) {
    unsigned slot = kernel->call_cursor++ & WL_KERNEL_CALLS_MASK;
    wl_kernel_call_t *call = &kernel->calls[slot];
    if (call->generation != generations[slot] || call->state == CALL_FREE)
      continue;
    if (call->state == CALL_WAITING && !expired(now, call->deadline)) continue;
    wl_kernel_result_t result =
        call->state == CALL_TERMINAL
            ? call->result
            : (wl_kernel_result_t){WL_KERNEL_TIMED_OUT, WL_ERR_TIMEOUT};
    finish_call(kernel, slot, result, NULL, 0U);
    return 1;
  }
  return 0;
}

WL_HOT_ATTR static int submit_tx(wl_kernel_t *kernel, wl_err_t *out_error) {
  if (kernel->closed || kernel->tx_blocked || wl_packet_tx_active(kernel->port))
    return 0;
  while (kernel->tx_count != 0U) {
    unsigned index = kernel->tx_queue[kernel->tx_head];
    wl_kernel_tx_t *tx = &kernel->tx[index];
    if (tx->state == READY && index < WL_KERNEL_CALLS &&
        (kernel->calls[tx->call_slot].state != CALL_WAITING ||
         kernel->calls[tx->call_slot].generation != tx->generation))
      tx->state = FREE;
    if (tx->state != READY) {
      queue_pop(kernel);
      continue;
    }
    wl_err_t error = wl_packet_tx_submit(kernel->port, tx_buf(kernel, index),
                                         tx->length, &kernel->flight_token);
    if (error == WL_ERR_WOULD_BLOCK) {
      kernel->tx_blocked = 1U;
      return 1;
    }
    queue_pop(kernel);
    if (error == WL_OK) {
      kernel->flight = (uint8_t)index;
      tx->state = LEASED;
      if (index < WL_KERNEL_CALLS) {
        wl_kernel_call_t *call = &kernel->calls[tx->call_slot];
        if (call->state == CALL_WAITING && call->generation == tx->generation)
          call->sent = 1U;
      }
    } else {
      fail_tx_call(kernel, index, error);
      tx->state = FREE;
      *out_error = report(kernel, tx->message_id, error);
    }
    return 1;
  }
  return 0;
}

WL_HOT_ATTR static int receive(wl_kernel_t *kernel, wl_err_t *out_error) {
  if (kernel->rx_blocked && !kernel->closed &&
      free_tx(kernel, REPLY_FIRST, MESSAGE_TX) < 0)
    return 0;
  if (!kernel->rx_held) {
    if (wl_packet_rx_borrow(kernel->port, &kernel->rx) != WL_OK) return 0;
    kernel->rx_held = 1U;
  }
  wl_kernel_frame_t frame = {0};
  wl_err_t error = WL_OK;
  if (!kernel->closed) {
    error = wl_wire_frame_read(kernel->rx.data, kernel->rx.length, &frame);
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

WL_HOT_ATTR wl_err_t wl_kernel_step(wl_kernel_t *kernel, uint32_t now,
                                    unsigned budget) {
  if (kernel == NULL || budget == 0U || budget > WL_KERNEL_BUDGET)
    return WL_ERR_INVALID_ARG;
  if (kernel->in_step) return WL_ERR_REENTRANT;
  kernel->in_step = 1U;
  uint32_t generations[WL_KERNEL_CALLS];
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i)
    generations[i] = kernel->calls[i].generation;
  if (wl_packet_take_activity(kernel->port)) kernel->tx_blocked = 0U;
  unsigned count = 0U;
  wl_err_t result = WL_OK;
  while (count < budget) {
    unsigned before = count;
    wl_err_t error = WL_OK;
    if (wl_packet_tx_ready(kernel->port)) {
      error = complete_tx(kernel);
      ++count;
    }
    /* Every eligible terminal in this step's generation snapshot precedes
     * RX. If the budget ends here, leave RX queued for a later owner pass. */
    while (count < budget && terminal(kernel, now, generations)) ++count;
    /* Single-site alternating TX/RX phase loop preserves budget=1 turn
     * fairness without duplicating the receive() call inside step(). */
    uint8_t do_rx = kernel->rx_turn;
    for (unsigned phase = 0U; phase < 2U && count < budget; ++phase) {
      int progressed = do_rx ? receive(kernel, &error)
                             : submit_tx(kernel, &error);
      if (progressed) {
        ++count;
        kernel->rx_turn = (uint8_t)!do_rx;
      }
      do_rx = !do_rx;
    }
    if (result == WL_OK) result = error;
    if (before == count) break;
  }
  kernel->in_step = 0U;
  return result;
}

WL_HOT_ATTR int wl_kernel_ready(const wl_kernel_t *kernel, uint32_t now,
                                int *has_deadline, uint32_t *deadline) {
  *has_deadline = 0;
  int ready = wl_packet_tx_ready(kernel->port);
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i) {
    const wl_kernel_call_t *call = &kernel->calls[i];
    if (call->state == CALL_FREE) continue;
    if (call->state == CALL_TERMINAL || expired(now, call->deadline))
      ready = 1;
    else if (!*has_deadline ||
             (uint32_t)(call->deadline - now) < (uint32_t)(*deadline - now)) {
      *has_deadline = 1;
      *deadline = call->deadline;
    }
  }
  if (wl_packet_rx_ready(kernel->port) &&
      (!kernel->rx_blocked || kernel->closed ||
       free_tx(kernel, REPLY_FIRST, MESSAGE_TX) >= 0))
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
  kernel->tx_count = 0U;
  wl_packet_stop(kernel->port);
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i)
    fail_call(&kernel->calls[i], WL_KERNEL_CLOSED, WL_ERR_INVALID_STATE);
  for (unsigned i = 0U; i < WL_KERNEL_TX_COUNT; ++i)
    if (kernel->tx[i].state != LEASED) kernel->tx[i].state = FREE;
  return WL_OK;
}

int wl_kernel_quiescent(const wl_kernel_t *kernel) {
  if (!kernel->closed || kernel->in_step || !wl_packet_quiescent(kernel->port))
    return 0;
  for (unsigned i = 0U; i < WL_KERNEL_CALLS; ++i)
    if (kernel->calls[i].state != CALL_FREE) return 0;
  return 1;
}
