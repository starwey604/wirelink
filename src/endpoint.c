/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/endpoint.h"

#include <limits.h>
#include <string.h>

enum { FREE, BUILDING, READY, LEASED };
enum { CALL_FREE, CALL_WAITING, CALL_TERMINAL };
#define NONE UINT8_MAX
#define REPLY_FIRST WL_ENDPOINT_CALLS
#define MESSAGE_TX (WL_ENDPOINT_TX_COUNT - 1U)
#define CALL_PREFIX (WL_WIRE_NATIVE_HEADER + WL_WIRE_RPC_HEADER_DATA)

static uint32_t instance(const wl_endpoint_t *endpoint) {
  return endpoint->port->config.instance;
}

static int expired(uint32_t now, uint32_t deadline) {
  return (uint32_t)(now - deadline) <= INT32_MAX;
}

static uint8_t *tx_buf(const wl_endpoint_t *endpoint, unsigned index) {
  if (index < WL_ENDPOINT_CALLS)
    return endpoint->storage.requests + index * (size_t)endpoint->storage.request_stride;
  if (index < MESSAGE_TX)
    return endpoint->storage.replies + (index - WL_ENDPOINT_CALLS) * (size_t)endpoint->storage.reply_stride;
  return endpoint->storage.message;
}

static int free_tx(const wl_endpoint_t *endpoint, unsigned first, unsigned end) {
  for (unsigned i = first; i < end; ++i)
    if (endpoint->tx[i].state == FREE) return (int)i;
  return -1;
}

/* Admission order across requests, replies and ordinary messages is observable
 * by applications (e.g. Progress followed by Control STOP). Eight TX slots let
 * every ring calculation collapse into a single bitmask instruction. */
static void queue_push(wl_endpoint_t *endpoint, unsigned index) {
  unsigned tail = (endpoint->tx_head + endpoint->tx_count) & WL_ENDPOINT_TX_MASK;
  endpoint->tx_queue[tail] = (uint8_t)index;
  ++endpoint->tx_count;
}

static void queue_pop(wl_endpoint_t *endpoint) {
  endpoint->tx_head = (uint8_t)((endpoint->tx_head + 1U) & WL_ENDPOINT_TX_MASK);
  --endpoint->tx_count;
}

static void queue_remove(wl_endpoint_t *endpoint, unsigned index) {
  unsigned kept = 0U, count = endpoint->tx_count;
  for (unsigned n = 0U; n < count; ++n) {
    uint8_t value = endpoint->tx_queue[(endpoint->tx_head + n) & WL_ENDPOINT_TX_MASK];
    if (value != index) {
      endpoint->tx_queue[(endpoint->tx_head + kept) & WL_ENDPOINT_TX_MASK] = value;
      ++kept;
    }
  }
  endpoint->tx_count = (uint8_t)kept;
}

static wl_err_t seal_and_queue(wl_endpoint_t *endpoint, unsigned index, uint8_t *frame,
                               size_t capacity, uint8_t kind, uint16_t id,
                               size_t wire_payload) {
  wl_err_t error = wl_wire_frame_seal(frame, capacity, kind, id, wire_payload);
  if (error != WL_OK) return error;
  wl_endpoint_tx_t *tx = &endpoint->tx[index];
  tx->message_id = id;
  tx->length = (uint16_t)(WL_WIRE_NATIVE_HEADER + wire_payload);
  tx->state = READY;
  queue_push(endpoint, index);
  return WL_OK;
}

static void fail_call(wl_endpoint_call_t *call, wl_rpc_outcome_t outcome,
                      int32_t status) {
  if (call->state == CALL_WAITING) {
    call->state = CALL_TERMINAL;
    call->result = (wl_rpc_result_t){outcome, status};
  }
}

static void fail_tx_call(wl_endpoint_t *endpoint, unsigned index, wl_err_t status) {
  if (index < WL_ENDPOINT_CALLS) {
    const wl_endpoint_tx_t *tx = &endpoint->tx[index];
    wl_endpoint_call_t *call = &endpoint->calls[tx->call_slot];
    if (call->generation == tx->generation)
      fail_call(call, WL_RPC_LOCAL_ERROR, status);
  }
}

static wl_err_t find_call_slot(const wl_endpoint_t *endpoint, int *out_slot) {
  if (endpoint->operation == UINT32_MAX) return WL_ERR_ID_EXHAUSTED;
  int exhausted = 0;
  for (unsigned i = 0U; i < WL_ENDPOINT_CALLS; ++i) {
    if (endpoint->calls[i].state == CALL_FREE) {
      if (endpoint->calls[i].generation != UINT32_MAX) {
        *out_slot = (int)i;
        return WL_OK;
      }
      exhausted = 1;
    }
  }
  return exhausted ? WL_ERR_ID_EXHAUSTED : WL_ERR_WOULD_BLOCK;
}

static int valid_storage(const uint8_t *data, uint16_t capacity, uint16_t minimum) {
  return data == NULL ? capacity == 0U : capacity >= minimum;
}

wl_err_t wl_endpoint_init(wl_endpoint_t *endpoint, const wl_endpoint_config_t *config) {
  if (endpoint == NULL || config == NULL || config->port == NULL ||
      config->port->config.instance == 0U ||
      !valid_storage(config->storage.requests, config->storage.request_stride, CALL_PREFIX) ||
      !valid_storage(config->storage.replies, config->storage.reply_stride,
                     WL_WIRE_NATIVE_HEADER + WL_WIRE_RPC_HEADER_ERR) ||
      !valid_storage(config->storage.message, config->storage.message_capacity,
                     WL_WIRE_NATIVE_HEADER))
    return WL_ERR_INVALID_ARG;
  const wl_endpoint_config_t value = *config;
  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->port = value.port;
  endpoint->storage = value.storage;
  endpoint->route = value.route;
  endpoint->on_error = value.on_error;
  endpoint->user = value.user;
  endpoint->flight = NONE;
  return WL_OK;
}

WL_HOT_ATTR wl_err_t wl_endpoint_msg_claim(wl_endpoint_t *endpoint,
                                         uint8_t **out_frame,
                                         size_t *out_capacity) {
  if (endpoint == NULL || out_frame == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->closed) return WL_ERR_INVALID_STATE;
  if (endpoint->storage.message == NULL) return WL_ERR_NOT_SUPPORTED;
  if (endpoint->tx[MESSAGE_TX].state != FREE) return WL_ERR_WOULD_BLOCK;
  *out_frame = endpoint->storage.message;
  if (out_capacity != NULL) *out_capacity = endpoint->storage.message_capacity;
  return WL_OK;
}

WL_HOT_ATTR wl_err_t wl_endpoint_msg_commit(wl_endpoint_t *endpoint,
                                          size_t frame_length) {
  if (endpoint == NULL || frame_length < WL_WIRE_NATIVE_HEADER)
    return WL_ERR_INVALID_ARG;
  if (endpoint->closed) return WL_ERR_INVALID_STATE;
  if (endpoint->storage.message == NULL) return WL_ERR_NOT_SUPPORTED;
  if (endpoint->tx[MESSAGE_TX].state != FREE) return WL_ERR_WOULD_BLOCK;
  if (frame_length - WL_WIRE_NATIVE_HEADER > WL_WIRE_MAX_PAYLOAD)
    return WL_ERR_PAYLOAD_TOO_LONG;
  if (frame_length > endpoint->storage.message_capacity)
    return WL_ERR_BUF_TOO_SMALL;
  uint8_t *frame = endpoint->storage.message;
  uint16_t id = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8U);
  if (frame[0] != WL_WIRE_MAGIC || frame[1] != WL_WIRE_MSG || id == 0U)
    return WL_ERR_BAD_FRAME;
  wl_endpoint_tx_t *tx = &endpoint->tx[MESSAGE_TX];
  tx->message_id = id;
  tx->length = (uint16_t)frame_length;
  tx->state = READY;
  queue_push(endpoint, MESSAGE_TX);
  return WL_OK;
}

WL_HOT_ATTR wl_err_t wl_endpoint_call_claim(wl_endpoint_t *endpoint,
                                          uint8_t *out_slot,
                                          uint8_t **out_payload,
                                          size_t *out_capacity) {
  if (endpoint == NULL || out_slot == NULL || out_payload == NULL)
    return WL_ERR_INVALID_ARG;
  if (endpoint->closed) return WL_ERR_INVALID_STATE;
  if (endpoint->storage.requests == NULL) return WL_ERR_NOT_SUPPORTED;
  int call_slot = -1;
  wl_err_t error = find_call_slot(endpoint, &call_slot);
  if (error != WL_OK) return error;
  int tx_index = free_tx(endpoint, 0U, WL_ENDPOINT_CALLS);
  if (tx_index < 0) return WL_ERR_WOULD_BLOCK;
  *out_slot = (uint8_t)tx_index;
  *out_payload = tx_buf(endpoint, (unsigned)tx_index) + CALL_PREFIX;
  if (out_capacity != NULL)
    *out_capacity = (size_t)endpoint->storage.request_stride - CALL_PREFIX;
  return WL_OK;
}

WL_HOT_ATTR wl_err_t wl_endpoint_call_commit(
    wl_endpoint_t *endpoint, uint8_t slot, uint16_t request_id,
    uint16_t response_id, size_t body_length, uint32_t now, uint32_t timeout, uint8_t has_now,
    wl_endpoint_complete_fn complete, void (*callback)(void), void *user,
    wl_call_handle_t *out) {
  if (out != NULL) *out = (wl_call_handle_t){0};
  if (endpoint == NULL || slot >= WL_ENDPOINT_CALLS ||
      request_id == 0U || response_id == 0U || request_id == response_id ||
      complete == NULL || callback == NULL || timeout == 0U ||
      timeout > INT32_MAX)
    return WL_ERR_INVALID_ARG;
  if (endpoint->closed) return WL_ERR_INVALID_STATE;
  if (endpoint->storage.requests == NULL) return WL_ERR_NOT_SUPPORTED;
  wl_endpoint_tx_t *tx = &endpoint->tx[slot];
  if (tx->state != FREE) return WL_ERR_INVALID_STATE;
  int call_slot = -1;
  wl_err_t error = find_call_slot(endpoint, &call_slot);
  if (error != WL_OK) return error;
  if (body_length > WL_WIRE_MAX_PAYLOAD - WL_WIRE_RPC_HEADER_DATA)
    return WL_ERR_PAYLOAD_TOO_LONG;
  uint8_t *frame = tx_buf(endpoint, slot);
  error = seal_and_queue(endpoint, slot, frame, endpoint->storage.request_stride,
                         WL_WIRE_RPC_REQ, request_id,
                         WL_WIRE_RPC_HEADER_DATA + body_length);
  if (error != WL_OK) return error;
  uint32_t operation = endpoint->operation + 1U;
  wl_wire_put_u32(frame + WL_WIRE_NATIVE_HEADER, instance(endpoint));
  wl_wire_put_u32(frame + WL_WIRE_NATIVE_HEADER + 4U, operation);
  wl_endpoint_call_t *call = &endpoint->calls[call_slot];
  ++call->generation;
  call->operation = operation;
  call->response_id = response_id;
  call->deadline = has_now ? now + timeout : timeout;
  call->armed = has_now != 0U;
  call->complete = complete;
  call->callback = callback;
  call->user = user;
  call->sent = 0U;
  call->state = CALL_WAITING;
  tx->call_slot = (uint8_t)call_slot;
  tx->generation = call->generation;
  endpoint->operation = operation;
  if (out != NULL) *out = (wl_call_handle_t){
      instance(endpoint), call->generation, operation, (uint8_t)call_slot};
  return WL_OK;
}

wl_err_t wl_endpoint_cancel(wl_endpoint_t *endpoint, wl_call_handle_t handle) {
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (handle.instance != instance(endpoint) || handle.slot >= WL_ENDPOINT_CALLS)
    return WL_ERR_NOT_FOUND;
  wl_endpoint_call_t *call = &endpoint->calls[handle.slot];
  if (call->state != CALL_WAITING || call->generation != handle.generation ||
      call->operation != handle.operation)
    return WL_ERR_NOT_FOUND;
  fail_call(call, WL_RPC_CANCELLED, WL_ERR_CANCELLED);
  return WL_OK;
}

WL_HOT_ATTR wl_err_t wl_endpoint_reply_claim(
    wl_endpoint_t *endpoint, uint16_t response_id,
    const wl_rpc_metadata_t *request, uint8_t *out_slot,
    uint8_t **out_payload, size_t *out_capacity) {
  if (endpoint == NULL || !endpoint->in_step || endpoint->closed)
    return WL_ERR_INVALID_STATE;
  if (response_id == 0U || request == NULL || request->session == 0U ||
      request->operation == 0U || out_slot == NULL || out_payload == NULL)
    return WL_ERR_INVALID_ARG;
  if (endpoint->storage.replies == NULL) return WL_ERR_NOT_SUPPORTED;
  int index = free_tx(endpoint, REPLY_FIRST, MESSAGE_TX);
  if (index < 0) return WL_ERR_WOULD_BLOCK;
  uint8_t *frame = tx_buf(endpoint, (unsigned)index);
  endpoint->tx[index].state = BUILDING;
  endpoint->tx[index].message_id = response_id;
  wl_wire_put_u32(frame + WL_WIRE_NATIVE_HEADER, request->session);
  wl_wire_put_u32(frame + WL_WIRE_NATIVE_HEADER + 4U, request->operation);
  *out_slot = (uint8_t)index;
  *out_payload = frame + CALL_PREFIX;
  if (out_capacity != NULL)
    *out_capacity = (size_t)endpoint->storage.reply_stride - CALL_PREFIX;
  return WL_OK;
}

WL_HOT_ATTR void wl_endpoint_reply_abort(wl_endpoint_t *endpoint, uint8_t slot) {
  if (endpoint != NULL && slot >= REPLY_FIRST && slot < MESSAGE_TX &&
      endpoint->tx[slot].state == BUILDING)
    endpoint->tx[slot].state = FREE;
}

static wl_err_t reply_finish(wl_endpoint_t *endpoint, uint8_t slot, uint8_t kind,
                             size_t wire_payload, int32_t status) {
  if (endpoint == NULL || !endpoint->in_step || slot < REPLY_FIRST ||
      slot >= MESSAGE_TX || endpoint->tx[slot].state != BUILDING)
    return WL_ERR_INVALID_ARG;
  wl_endpoint_tx_t *tx = &endpoint->tx[slot];
  if (kind == WL_WIRE_RPC_ERR) {
    if (status == 0) {
      tx->state = FREE;
      return WL_ERR_INVALID_ARG;
    }
    wl_wire_put_u32(tx_buf(endpoint, slot) + CALL_PREFIX, (uint32_t)status);
  }
  wl_err_t error = seal_and_queue(endpoint, slot, tx_buf(endpoint, slot),
                                  endpoint->storage.reply_stride, kind,
                                  tx->message_id, wire_payload);
  if (error != WL_OK) tx->state = FREE;
  return error;
}

WL_HOT_ATTR wl_err_t wl_endpoint_reply_commit(wl_endpoint_t *endpoint, uint8_t slot,
                                            size_t body_length) {
  if (body_length > WL_WIRE_MAX_PAYLOAD - WL_WIRE_RPC_HEADER_DATA) {
    wl_endpoint_reply_abort(endpoint, slot);
    return WL_ERR_PAYLOAD_TOO_LONG;
  }
  return reply_finish(endpoint, slot, WL_WIRE_RPC_RESP,
                      WL_WIRE_RPC_HEADER_DATA + body_length, 0);
}

WL_HOT_ATTR wl_err_t wl_endpoint_reply_error(wl_endpoint_t *endpoint, uint8_t slot,
                                           int32_t status) {
  return reply_finish(endpoint, slot, WL_WIRE_RPC_ERR,
                      WL_WIRE_RPC_HEADER_ERR, status);
}

static void finish_call(wl_endpoint_t *endpoint, unsigned slot,
                        wl_rpc_result_t result, const uint8_t *body,
                        size_t length) {
  wl_endpoint_call_t saved = endpoint->calls[slot];
  /* Release logical ownership before callback so it may create another call.
   * Physical LEASED bytes keep their separate generation until DRAINED. */
  endpoint->calls[slot].state = CALL_FREE;
  for (unsigned i = 0U; i < WL_ENDPOINT_CALLS; ++i)
    if (endpoint->tx[i].state == READY && endpoint->tx[i].call_slot == slot &&
        endpoint->tx[i].generation == saved.generation) {
      endpoint->tx[i].state = FREE;
      queue_remove(endpoint, i);
    }
  saved.complete(endpoint, &saved, result, body, length);
}

WL_HOT_ATTR wl_err_t wl_endpoint_response(wl_endpoint_t *endpoint,
                                        const wl_wire_frame_t *frame) {
  if (endpoint == NULL || frame == NULL) return WL_ERR_INVALID_ARG;
  if (!endpoint->in_step) return WL_ERR_INVALID_STATE;
  if (frame->kind != WL_WIRE_RPC_RESP && frame->kind != WL_WIRE_RPC_ERR)
    return WL_ERR_CORRUPT_PAYLOAD;
  wl_rpc_metadata_t metadata;
  wl_err_t error =
      wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
  if (error != WL_OK) return error;
  if (metadata.session != instance(endpoint)) return WL_OK;
  for (unsigned i = 0U; i < WL_ENDPOINT_CALLS; ++i) {
    wl_endpoint_call_t *call = &endpoint->calls[i];
    if (call->state != CALL_WAITING || !call->sent ||
        call->operation != metadata.operation ||
        call->response_id != frame->message_id)
      continue;
    wl_rpc_result_t result = {
        metadata.status == 0 ? WL_RPC_SUCCESS : WL_RPC_REJECTED,
        metadata.status};
    finish_call(endpoint, i, result, metadata.body, metadata.length);
    break;
  }
  return WL_OK;
}

static wl_err_t report(wl_endpoint_t *endpoint, uint16_t id, wl_err_t error) {
  if (error != WL_OK && endpoint->on_error != NULL)
    endpoint->on_error(endpoint->user, id, error);
  return error;
}

WL_HOT_ATTR static wl_err_t complete_tx(wl_endpoint_t *endpoint) {
  wl_packet_token_t token;
  unsigned flags = wl_packet_tx_take(endpoint->port, &token);
  if (flags == 0U || endpoint->flight == NONE) return WL_OK;
  if (token.instance != endpoint->flight_token.instance ||
      token.sequence != endpoint->flight_token.sequence)
    return WL_ERR_INVALID_STATE;
  wl_endpoint_tx_t *tx = &endpoint->tx[endpoint->flight];
  wl_err_t error = WL_OK;
  if ((flags & WL_PACKET_TX_ERROR) != 0U) {
    fail_tx_call(endpoint, endpoint->flight, WL_ERR_IO);
    error = report(endpoint, tx->message_id, WL_ERR_IO);
  }
  if ((flags & WL_PACKET_DRAINED) != 0U) {
    tx->state = FREE;
    endpoint->flight = NONE;
    endpoint->tx_blocked = 0U;
  }
  return error;
}

WL_HOT_ATTR static int terminal(wl_endpoint_t *endpoint, uint32_t now,
                                const uint32_t *generations) {
  for (unsigned n = 0U; n < WL_ENDPOINT_CALLS; ++n) {
    unsigned slot = endpoint->call_cursor++ & WL_ENDPOINT_CALLS_MASK;
    wl_endpoint_call_t *call = &endpoint->calls[slot];
    if (call->generation != generations[slot] || call->state == CALL_FREE)
      continue;
    if (call->state == CALL_WAITING &&
        (!call->armed || !expired(now, call->deadline))) continue;
    wl_rpc_result_t result =
        call->state == CALL_TERMINAL
            ? call->result
            : (wl_rpc_result_t){WL_RPC_TIMED_OUT, WL_ERR_TIMEOUT};
    finish_call(endpoint, slot, result, NULL, 0U);
    return 1;
  }
  return 0;
}

WL_HOT_ATTR static int submit_tx(wl_endpoint_t *endpoint, wl_err_t *out_error) {
  if (endpoint->closed || endpoint->tx_blocked || wl_packet_tx_active(endpoint->port))
    return 0;
  while (endpoint->tx_count != 0U) {
    unsigned index = endpoint->tx_queue[endpoint->tx_head];
    wl_endpoint_tx_t *tx = &endpoint->tx[index];
    if (tx->state == READY && index < WL_ENDPOINT_CALLS &&
        (endpoint->calls[tx->call_slot].state != CALL_WAITING ||
         endpoint->calls[tx->call_slot].generation != tx->generation))
      tx->state = FREE;
    if (tx->state != READY) {
      queue_pop(endpoint);
      continue;
    }
    wl_err_t error = wl_packet_tx_submit(endpoint->port, tx_buf(endpoint, index),
                                         tx->length, &endpoint->flight_token);
    if (error == WL_ERR_WOULD_BLOCK) {
      endpoint->tx_blocked = 1U;
      return 1;
    }
    queue_pop(endpoint);
    if (error == WL_OK) {
      endpoint->flight = (uint8_t)index;
      tx->state = LEASED;
      if (index < WL_ENDPOINT_CALLS) {
        wl_endpoint_call_t *call = &endpoint->calls[tx->call_slot];
        if (call->state == CALL_WAITING && call->generation == tx->generation)
          call->sent = 1U;
      }
    } else {
      fail_tx_call(endpoint, index, error);
      tx->state = FREE;
      *out_error = report(endpoint, tx->message_id, error);
    }
    return 1;
  }
  return 0;
}

WL_HOT_ATTR static int receive(wl_endpoint_t *endpoint, wl_err_t *out_error) {
  if (endpoint->rx_blocked && !endpoint->closed &&
      free_tx(endpoint, REPLY_FIRST, MESSAGE_TX) < 0)
    return 0;
  if (!endpoint->rx_held) {
    if (wl_packet_rx_borrow(endpoint->port, &endpoint->rx) != WL_OK) return 0;
    endpoint->rx_held = 1U;
  }
  wl_wire_frame_t frame = {0};
  wl_err_t error = WL_OK;
  if (!endpoint->closed) {
    error = wl_wire_frame_read(endpoint->rx.data, endpoint->rx.length, &frame);
    if (error == WL_OK)
      error = endpoint->route != NULL ? endpoint->route(endpoint, &frame) : WL_ERR_NOT_SUPPORTED;
  }
  if (error == WL_ERR_WOULD_BLOCK) {
    endpoint->rx_blocked = 1U;
    return 1;
  }
  endpoint->rx_blocked = 0U;
  endpoint->rx_held = 0U;
  (void)wl_packet_rx_release(endpoint->port, &endpoint->rx);
  if (error != WL_OK) *out_error = report(endpoint, frame.message_id, error);
  return 1;
}

WL_HOT_ATTR wl_err_t wl_endpoint_step(wl_endpoint_t *endpoint, uint32_t now,
                                    unsigned budget) {
  if (endpoint == NULL || budget == 0U || budget > WL_ENDPOINT_BUDGET)
    return WL_ERR_INVALID_ARG;
  if (endpoint->in_step) return WL_ERR_REENTRANT;
  endpoint->in_step = 1U;
  uint32_t generations[WL_ENDPOINT_CALLS];
  for (unsigned i = 0U; i < WL_ENDPOINT_CALLS; ++i) {
    wl_endpoint_call_t *call = &endpoint->calls[i];
    if (call->state == CALL_WAITING && !call->armed) {
      call->deadline += now;
      call->armed = 1U;
    }
    generations[i] = endpoint->calls[i].generation;
  }
  if (wl_packet_take_activity(endpoint->port)) endpoint->tx_blocked = 0U;
  unsigned count = 0U;
  wl_err_t result = WL_OK;
  while (count < budget) {
    unsigned before = count;
    wl_err_t error = WL_OK;
    if (wl_packet_tx_ready(endpoint->port)) {
      error = complete_tx(endpoint);
      ++count;
    }
    /* Every eligible terminal in this step's generation snapshot precedes
     * RX. If the budget ends here, leave RX queued for a later owner pass. */
    while (count < budget && terminal(endpoint, now, generations)) ++count;
    /* Single-site alternating TX/RX phase loop preserves budget=1 turn
     * fairness without duplicating the receive() call inside step(). */
    uint8_t do_rx = endpoint->rx_turn;
    for (unsigned phase = 0U; phase < 2U && count < budget; ++phase) {
      int progressed = do_rx ? receive(endpoint, &error)
                             : submit_tx(endpoint, &error);
      if (progressed) {
        ++count;
        endpoint->rx_turn = (uint8_t)!do_rx;
      }
      do_rx = !do_rx;
    }
    if (result == WL_OK) result = error;
    if (before == count) break;
  }
  endpoint->in_step = 0U;
  return result;
}

WL_HOT_ATTR int wl_endpoint_ready(const wl_endpoint_t *endpoint, uint32_t now,
                                int *has_deadline, uint32_t *deadline) {
  *has_deadline = 0;
  int ready = wl_packet_tx_ready(endpoint->port);
  for (unsigned i = 0U; i < WL_ENDPOINT_CALLS; ++i) {
    const wl_endpoint_call_t *call = &endpoint->calls[i];
    if (call->state == CALL_FREE) continue;
    if (call->state == CALL_WAITING && !call->armed) {
      ready = 1;
      continue;
    }
    if (call->state == CALL_TERMINAL || expired(now, call->deadline))
      ready = 1;
    else if (!*has_deadline ||
             (uint32_t)(call->deadline - now) < (uint32_t)(*deadline - now)) {
      *has_deadline = 1;
      *deadline = call->deadline;
    }
  }
  if (wl_packet_rx_ready(endpoint->port) &&
      (!endpoint->rx_blocked || endpoint->closed ||
       free_tx(endpoint, REPLY_FIRST, MESSAGE_TX) >= 0))
    ready = 1;
  if (!endpoint->closed && !wl_packet_tx_active(endpoint->port) &&
      (!endpoint->tx_blocked || wl_packet_activity_pending(endpoint->port)))
    for (unsigned i = 0U; i < WL_ENDPOINT_TX_COUNT; ++i)
      if (endpoint->tx[i].state == READY) ready = 1;
  return ready;
}

wl_err_t wl_endpoint_close(wl_endpoint_t *endpoint) {
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->in_step) return WL_ERR_REENTRANT;
  endpoint->closed = 1U;
  endpoint->tx_count = 0U;
  wl_packet_stop(endpoint->port);
  for (unsigned i = 0U; i < WL_ENDPOINT_CALLS; ++i)
    fail_call(&endpoint->calls[i], WL_RPC_CLOSED, WL_ERR_INVALID_STATE);
  for (unsigned i = 0U; i < WL_ENDPOINT_TX_COUNT; ++i)
    if (endpoint->tx[i].state != LEASED) endpoint->tx[i].state = FREE;
  return WL_OK;
}

int wl_endpoint_quiescent(const wl_endpoint_t *endpoint) {
  if (!endpoint->closed || endpoint->in_step || !wl_packet_quiescent(endpoint->port))
    return 0;
  for (unsigned i = 0U; i < WL_ENDPOINT_CALLS; ++i)
    if (endpoint->calls[i].state != CALL_FREE) return 0;
  return 1;
}
