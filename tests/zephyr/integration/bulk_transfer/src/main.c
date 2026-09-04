/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "control_bindings.h"
#include "crc_internal.h"
#include "wirelink/bulk.h"
#include "wirelink/frame.h"
#include "wirelink/port.h"

#define OBJECT_LENGTH (UINT64_C(1024) * UINT64_C(1024))
#define CHUNK_SIZE 1024U
#define RESUME_OFFSET (UINT64_C(3) * CHUNK_SIZE)
#define TRANSPORT_MAX_PAYLOAD 1088U
#define TRANSPORT_UNIT_CAPACITY 1152U
#define CONTROL_UNIT_CAPACITY 64U
#define STATUS_TIMEOUT_MS 5U
#define DRIVER_STEP_LIMIT 4096U

struct endpoint {
  wl_ctx_t ctx;
  wl_config_t config;
  wl_storage_t storage;
  uint8_t tx_payload[TRANSPORT_MAX_PAYLOAD];
  uint8_t tx_unit[TRANSPORT_UNIT_CAPACITY];
  uint8_t control_unit[CONTROL_UNIT_CAPACITY];
  uint8_t rx_fifo[TRANSPORT_UNIT_CAPACITY];
  uint8_t rx_fallback[TRANSPORT_UNIT_CAPACITY];
  uint8_t outbound[TRANSPORT_UNIT_CAPACITY];
  size_t outbound_length;
};

struct object_sink {
  const wl_event_t *active_event;
  uint64_t resume_offset;
  uint64_t next_offset;
  uint32_t crc_state;
  uint32_t begin_calls;
  uint32_t write_calls;
  uint32_t finish_calls;
  uint32_t commit_calls;
  uint32_t abort_calls;
  bool dispatch_active;
  bool invalid_data;
  bool observed_borrowed_chunk;
};

struct receiver_adapter {
  wl_bulk_receiver_t *receiver;
  struct endpoint *endpoint;
  struct object_sink *sink;
  const wl_event_t *active_event;
  wl_time_ms_t now_ms;
  bulk_begin_t begin_scratch;
  bulk_chunk_t chunk_scratch;
  bulk_end_t end_scratch;
  bulk_abort_t abort_scratch;
  control_router_t router;
};

struct sender_adapter {
  wl_bulk_sender_t *sender;
  const wl_event_t *active_event;
  wl_time_ms_t now_ms;
  bulk_status_t status_scratch;
  wl_bulk_status_t last_status;
  uint32_t status_calls;
  control_router_t router;
};

struct transfer_fixture {
  struct endpoint sender_endpoint;
  struct endpoint receiver_endpoint;
  wl_bulk_sender_t sender;
  wl_bulk_receiver_t receiver;
  struct object_sink sink;
  struct receiver_adapter receiver_adapter;
  struct sender_adapter sender_adapter;
  uint8_t encode_scratch[TRANSPORT_MAX_PAYLOAD];
  uint8_t source_chunk[CHUNK_SIZE];
  wl_time_ms_t now_ms;
  bool drop_first_chunk_status;
  bool status_was_dropped;
};

static struct transfer_fixture fixture;

static uint8_t object_byte(uint64_t offset) {
  uint32_t position = (uint32_t)offset;

  return (uint8_t)((position * 29U) ^ (position >> 8U) ^ (position >> 16U));
}

static void fill_object_span(uint64_t offset, uint8_t *out, size_t length) {
  for (size_t index = 0U; index < length; ++index) {
    out[index] = object_byte(offset + (uint64_t)index);
  }
}

static uint32_t object_crc_state(uint64_t length) {
  uint8_t block[CHUNK_SIZE];
  uint32_t state = UINT32_MAX;
  uint64_t offset = 0U;

  while (offset < length) {
    uint64_t remaining = length - offset;
    size_t block_length =
        remaining < sizeof(block) ? (size_t)remaining : sizeof(block);

    fill_object_span(offset, block, block_length);
    state = wl_crc32c_update(state, block, block_length);
    offset += block_length;
  }
  return state;
}

static uint32_t object_crc32c(uint64_t length) {
  return object_crc_state(length) ^ UINT32_MAX;
}

static bool span_contains(const uint8_t *outer, size_t outer_length,
                          const uint8_t *inner, size_t inner_length) {
  uintptr_t outer_address;
  uintptr_t inner_address;
  size_t offset;

  if (outer == NULL || inner == NULL) {
    return false;
  }
  outer_address = (uintptr_t)outer;
  inner_address = (uintptr_t)inner;
  if (inner_address < outer_address) {
    return false;
  }
  offset = (size_t)(inner_address - outer_address);
  return offset <= outer_length && inner_length <= outer_length - offset;
}

static bool endpoint_owns_payload(const struct endpoint *endpoint,
                                  const wl_event_t *event) {
  return span_contains(endpoint->rx_fifo, endpoint->storage.rx_fifo_size,
                       event->payload, event->payload_len) ||
         span_contains(endpoint->rx_fallback,
                       endpoint->storage.rx_fallback_size, event->payload,
                       event->payload_len);
}

static wl_sink_result_t endpoint_sink(void *user_data, wl_io_token_t token,
                                      const uint8_t *data, size_t length) {
  struct endpoint *endpoint = user_data;

  (void)token;
  if (endpoint->outbound_length != 0U || length > sizeof(endpoint->outbound)) {
    return WL_SINK_BUSY;
  }
  memcpy(endpoint->outbound, data, length);
  endpoint->outbound_length = length;
  return WL_SINK_SENT;
}

static void endpoint_init(struct endpoint *endpoint, uint64_t session_id) {
  wl_storage_requirements_t requirements;

  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->config = (wl_config_t){
      .max_payload_len = TRANSPORT_MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .integrity = WL_INTEGRITY_NONE,
      .session_id = session_id,
      .max_retries = 0U,
      .ack_timeout_ms = 10U,
  };
  zassert_ok(wl_config_requirements(&endpoint->config, &requirements));
  zassert_true(requirements.tx_payload_size <= sizeof(endpoint->tx_payload));
  zassert_true(requirements.tx_unit_size <= sizeof(endpoint->tx_unit));
  zassert_true(requirements.control_unit_size <=
               sizeof(endpoint->control_unit));
  zassert_true(requirements.rx_fifo_size <= sizeof(endpoint->rx_fifo));
  zassert_true(requirements.rx_fallback_size <= sizeof(endpoint->rx_fallback));

  endpoint->storage = (wl_storage_t){
      .tx_payload = endpoint->tx_payload,
      .tx_payload_size = requirements.tx_payload_size,
      .tx_unit = endpoint->tx_unit,
      .tx_unit_size = requirements.tx_unit_size,
      .control_unit = endpoint->control_unit,
      .control_unit_size = requirements.control_unit_size,
      .rx_fifo = endpoint->rx_fifo,
      .rx_fifo_size = requirements.rx_fifo_size,
      .rx_fallback = endpoint->rx_fallback,
      .rx_fallback_size = requirements.rx_fallback_size,
  };
  zassert_ok(wl_init(&endpoint->ctx, &endpoint->config, &endpoint->storage));
  zassert_ok(wl_set_sink(&endpoint->ctx, endpoint_sink, endpoint));
}

static void drain_tx_success(struct endpoint *endpoint, wl_time_ms_t now_ms) {
  wl_event_t event = {0};

  zassert_ok(wl_poll(&endpoint->ctx, now_ms, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
}

static control_dispatch_result_t
deliver_to_router(struct endpoint *source, struct endpoint *destination,
                  control_router_t *router, const wl_event_t **active_event,
                  wl_time_ms_t now_ms) {
  wl_event_t event = {0};
  control_dispatch_result_t result;
  size_t accepted = 0U;

  zassert_not_equal(source->outbound_length, 0U);
  zassert_ok(wl_feed_bytes(&destination->ctx, source->outbound,
                           source->outbound_length, &accepted));
  zassert_equal(accepted, source->outbound_length);
  source->outbound_length = 0U;
  zassert_ok(wl_poll(&destination->ctx, now_ms, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  *active_event = &event;
  result = control_dispatch_event(&destination->ctx, &event, router);
  *active_event = NULL;
  zassert_equal(result.domain, CONTROL_DISPATCH_OK);
  zassert_equal(result.codec_status, WL_CODEC_OK);
  return result;
}

static wl_bulk_sink_result_t
object_sink_begin(void *user_data, const wl_bulk_descriptor_t *descriptor,
                  uint64_t *out_resume_offset) {
  struct object_sink *sink = user_data;

  ++sink->begin_calls;
  if (sink->resume_offset > descriptor->total_length ||
      sink->resume_offset % CHUNK_SIZE != 0U) {
    return WL_BULK_SINK_INVALID;
  }
  sink->next_offset = sink->resume_offset;
  sink->crc_state = object_crc_state(sink->resume_offset);
  *out_resume_offset = sink->resume_offset;
  return WL_BULK_SINK_OK;
}

static wl_bulk_sink_result_t
object_sink_write(void *user_data, uint32_t transfer_id, uint64_t offset,
                  const uint8_t *data, size_t length) {
  struct object_sink *sink = user_data;

  (void)transfer_id;
  ++sink->write_calls;
  if (!sink->dispatch_active || sink->active_event == NULL ||
      !span_contains(sink->active_event->payload,
                     sink->active_event->payload_len, data, length)) {
    sink->invalid_data = true;
    return WL_BULK_SINK_INVALID;
  }
  sink->observed_borrowed_chunk = true;
  if (offset != sink->next_offset) {
    sink->invalid_data = true;
    return WL_BULK_SINK_INVALID;
  }
  for (size_t index = 0U; index < length; ++index) {
    if (data[index] != object_byte(offset + (uint64_t)index)) {
      sink->invalid_data = true;
      return WL_BULK_SINK_INTEGRITY_FAILED;
    }
  }
  sink->crc_state = wl_crc32c_update(sink->crc_state, data, length);
  sink->next_offset += length;
  return WL_BULK_SINK_OK;
}

static wl_bulk_sink_result_t
object_sink_finish(void *user_data, const wl_bulk_descriptor_t *descriptor) {
  struct object_sink *sink = user_data;

  ++sink->finish_calls;
  if (sink->next_offset != descriptor->total_length) {
    return WL_BULK_SINK_INVALID;
  }
  if ((sink->crc_state ^ UINT32_MAX) != descriptor->object_crc32c) {
    return WL_BULK_SINK_INTEGRITY_FAILED;
  }
  ++sink->commit_calls;
  return WL_BULK_SINK_OK;
}

static void object_sink_abort(void *user_data, uint32_t transfer_id,
                              int32_t reason) {
  struct object_sink *sink = user_data;

  (void)transfer_id;
  (void)reason;
  ++sink->abort_calls;
}

static int32_t receive_begin(void *user_data, const bulk_begin_t *message,
                             wl_delivery_t delivery) {
  struct receiver_adapter *adapter = user_data;
  wl_bulk_descriptor_t descriptor;

  if (delivery != WL_DELIVERY_UNRELIABLE || !message->has_transfer_id ||
      !message->has_total_length || !message->has_requested_chunk_size ||
      !message->has_object_crc32c) {
    return -1;
  }
  descriptor = (wl_bulk_descriptor_t){
      .transfer_id = message->transfer_id,
      .total_length = message->total_length,
      .requested_chunk_size = message->requested_chunk_size,
      .object_crc32c = message->object_crc32c,
  };
  return wl_bulk_receiver_on_begin(adapter->receiver, &descriptor,
                                   adapter->now_ms) == WL_BULK_OK
             ? 0
             : -2;
}

static int32_t receive_chunk(void *user_data, const bulk_chunk_t *message,
                             wl_delivery_t delivery) {
  struct receiver_adapter *adapter = user_data;
  wl_bulk_chunk_t chunk;
  wl_bulk_err_t error;

  if (delivery != WL_DELIVERY_UNRELIABLE || !message->has_transfer_id ||
      !message->has_offset || !message->has_data ||
      adapter->active_event == NULL ||
      !endpoint_owns_payload(adapter->endpoint, adapter->active_event) ||
      !span_contains(adapter->active_event->payload,
                     adapter->active_event->payload_len, message->data.data,
                     message->data.length)) {
    return -1;
  }
  chunk = (wl_bulk_chunk_t){
      .transfer_id = message->transfer_id,
      .offset = message->offset,
      .data = message->data.data,
      .length = message->data.length,
  };
  adapter->sink->active_event = adapter->active_event;
  adapter->sink->dispatch_active = true;
  error = wl_bulk_receiver_on_chunk(adapter->receiver, &chunk, adapter->now_ms);
  adapter->sink->dispatch_active = false;
  adapter->sink->active_event = NULL;
  return error == WL_BULK_OK ? 0 : -2;
}

static int32_t receive_end(void *user_data, const bulk_end_t *message,
                           wl_delivery_t delivery) {
  struct receiver_adapter *adapter = user_data;

  if (delivery != WL_DELIVERY_UNRELIABLE || !message->has_transfer_id ||
      !message->has_total_length || !message->has_object_crc32c) {
    return -1;
  }
  return wl_bulk_receiver_on_end(adapter->receiver, message->transfer_id,
                                 message->total_length, message->object_crc32c,
                                 adapter->now_ms) == WL_BULK_OK
             ? 0
             : -2;
}

static int32_t receive_abort(void *user_data, const bulk_abort_t *message,
                             wl_delivery_t delivery) {
  struct receiver_adapter *adapter = user_data;

  if (delivery != WL_DELIVERY_UNRELIABLE || !message->has_transfer_id ||
      !message->has_reason) {
    return -1;
  }
  return wl_bulk_receiver_on_abort(adapter->receiver, message->transfer_id,
                                   message->reason,
                                   adapter->now_ms) == WL_BULK_OK
             ? 0
             : -2;
}

static int32_t receive_status(void *user_data, const bulk_status_t *message,
                              wl_delivery_t delivery) {
  struct sender_adapter *adapter = user_data;

  if (delivery != WL_DELIVERY_UNRELIABLE || !message->has_transfer_id ||
      !message->has_phase || !message->has_code || !message->has_next_offset ||
      !message->has_accepted_chunk_size) {
    return -1;
  }
  adapter->last_status = (wl_bulk_status_t){
      .transfer_id = message->transfer_id,
      .phase = (wl_bulk_phase_t)message->phase,
      .code = (wl_bulk_status_code_t)message->code,
      .next_offset = message->next_offset,
      .accepted_chunk_size = message->accepted_chunk_size,
  };
  ++adapter->status_calls;
  if (adapter->sender == NULL) {
    return 0;
  }
  return wl_bulk_sender_on_status(adapter->sender, &adapter->last_status,
                                  adapter->now_ms) == WL_BULK_OK
             ? 0
             : -2;
}

static void adapters_init(struct transfer_fixture *transfer) {
  struct receiver_adapter *receiver = &transfer->receiver_adapter;
  struct sender_adapter *sender = &transfer->sender_adapter;

  receiver->receiver = &transfer->receiver;
  receiver->endpoint = &transfer->receiver_endpoint;
  receiver->sink = &transfer->sink;
  receiver->router.bulk_begin = (control_bulk_begin_route_t){
      .scratch = &receiver->begin_scratch,
      .handler = receive_begin,
      .user_data = receiver,
  };
  receiver->router.bulk_chunk = (control_bulk_chunk_route_t){
      .scratch = &receiver->chunk_scratch,
      .handler = receive_chunk,
      .user_data = receiver,
  };
  receiver->router.bulk_end = (control_bulk_end_route_t){
      .scratch = &receiver->end_scratch,
      .handler = receive_end,
      .user_data = receiver,
  };
  receiver->router.bulk_abort = (control_bulk_abort_route_t){
      .scratch = &receiver->abort_scratch,
      .handler = receive_abort,
      .user_data = receiver,
  };

  sender->sender = &transfer->sender;
  sender->router.bulk_status = (control_bulk_status_route_t){
      .scratch = &sender->status_scratch,
      .handler = receive_status,
      .user_data = sender,
  };
}

static void transfer_init(uint64_t resume_offset) {
  wl_bulk_sender_config_t sender_config = {
      .status_timeout_ms = STATUS_TIMEOUT_MS,
      .busy_retry_ms = 1U,
      .max_retries = 2U,
  };
  wl_bulk_receiver_config_t receiver_config;

  memset(&fixture, 0, sizeof(fixture));
  fixture.now_ms = 1U;
  endpoint_init(&fixture.sender_endpoint, UINT64_C(0x1111222233334444));
  endpoint_init(&fixture.receiver_endpoint, UINT64_C(0xAAAABBBBCCCCDDDD));
  fixture.sink.resume_offset = resume_offset;
  receiver_config = (wl_bulk_receiver_config_t){
      .max_object_length = OBJECT_LENGTH,
      .max_chunk_size = CHUNK_SIZE,
      .write_alignment = 4U,
      .idle_timeout_ms = 0U,
      .sink =
          {
              .user_data = &fixture.sink,
              .begin = object_sink_begin,
              .write = object_sink_write,
              .finish = object_sink_finish,
              .abort = object_sink_abort,
          },
  };
  zassert_equal(wl_bulk_sender_init(&fixture.sender, &sender_config),
                WL_BULK_OK);
  zassert_equal(wl_bulk_receiver_init(&fixture.receiver, &receiver_config),
                WL_BULK_OK);
  adapters_init(&fixture);
}

static void assert_send_ok(control_send_result_t result) {
  zassert_equal(result.domain, CONTROL_SEND_OK);
  zassert_equal(result.codec_status, WL_CODEC_OK);
  zassert_equal(result.core_result, WL_OK);
}

static void send_action(const wl_bulk_sender_action_t *action,
                        wl_time_ms_t now_ms) {
  control_send_result_t send_result;

  switch (action->phase) {
  case WL_BULK_PHASE_BEGIN: {
    bulk_begin_t message = {
        .has_transfer_id = true,
        .transfer_id = action->descriptor.transfer_id,
        .has_total_length = true,
        .total_length = action->descriptor.total_length,
        .has_requested_chunk_size = true,
        .requested_chunk_size = action->descriptor.requested_chunk_size,
        .has_object_crc32c = true,
        .object_crc32c = action->descriptor.object_crc32c,
    };
    send_result = control_bulk_begin_send(&fixture.sender_endpoint.ctx,
                                          &message, WL_DELIVERY_UNRELIABLE);
    break;
  }
  case WL_BULK_PHASE_CHUNK: {
    bulk_chunk_t message;

    zassert_true(action->length <= sizeof(fixture.source_chunk));
    fill_object_span(action->offset, fixture.source_chunk, action->length);
    message = (bulk_chunk_t){
        .has_transfer_id = true,
        .transfer_id = action->descriptor.transfer_id,
        .has_offset = true,
        .offset = action->offset,
        .has_data = true,
        .data =
            {
                .data = fixture.source_chunk,
                .length = action->length,
            },
    };
    send_result = control_bulk_chunk_send(&fixture.sender_endpoint.ctx,
                                          &message, WL_DELIVERY_UNRELIABLE);
    break;
  }
  case WL_BULK_PHASE_END: {
    bulk_end_t message = {
        .has_transfer_id = true,
        .transfer_id = action->descriptor.transfer_id,
        .has_total_length = true,
        .total_length = action->descriptor.total_length,
        .has_object_crc32c = true,
        .object_crc32c = action->descriptor.object_crc32c,
    };
    send_result = control_bulk_end_send(&fixture.sender_endpoint.ctx, &message,
                                        WL_DELIVERY_UNRELIABLE);
    break;
  }
  case WL_BULK_PHASE_ABORT: {
    bulk_abort_t message = {
        .has_transfer_id = true,
        .transfer_id = action->descriptor.transfer_id,
        .has_reason = true,
        .reason = action->abort_reason,
    };
    send_result = control_bulk_abort_send(&fixture.sender_endpoint.ctx,
                                          &message, WL_DELIVERY_UNRELIABLE);
    break;
  }
  default:
    zassert_unreachable("invalid bulk sender phase");
    return;
  }

  assert_send_ok(send_result);
  zassert_equal(
      wl_bulk_sender_action_submitted(&fixture.sender, action, now_ms),
      WL_BULK_OK);
  drain_tx_success(&fixture.sender_endpoint, now_ms);
  fixture.receiver_adapter.now_ms = now_ms;
  (void)deliver_to_router(&fixture.sender_endpoint, &fixture.receiver_endpoint,
                          &fixture.receiver_adapter.router,
                          &fixture.receiver_adapter.active_event, now_ms);
}

static bool publish_receiver_status(wl_time_ms_t now_ms) {
  wl_bulk_receiver_status_view_t view;
  bulk_status_t message;
  control_send_result_t send_result;
  bool drop;

  zassert_equal(wl_bulk_receiver_status_acquire(&fixture.receiver, &view),
                WL_BULK_OK);
  message = (bulk_status_t){
      .has_transfer_id = true,
      .transfer_id = view.status.transfer_id,
      .has_phase = true,
      .phase = (control_bulk_phase_t)view.status.phase,
      .has_code = true,
      .code = (control_bulk_status_code_t)view.status.code,
      .has_next_offset = true,
      .next_offset = view.status.next_offset,
      .has_accepted_chunk_size = true,
      .accepted_chunk_size = view.status.accepted_chunk_size,
  };
  send_result = control_bulk_status_send(&fixture.receiver_endpoint.ctx,
                                         &message, WL_DELIVERY_UNRELIABLE);
  assert_send_ok(send_result);
  zassert_equal(wl_bulk_receiver_status_release(&fixture.receiver, &view),
                WL_BULK_OK);
  drain_tx_success(&fixture.receiver_endpoint, now_ms);

  drop = fixture.drop_first_chunk_status && !fixture.status_was_dropped &&
         view.status.phase == WL_BULK_PHASE_CHUNK &&
         view.status.code == WL_BULK_STATUS_OK;
  if (drop) {
    fixture.receiver_endpoint.outbound_length = 0U;
    fixture.status_was_dropped = true;
    return false;
  }

  fixture.sender_adapter.now_ms = now_ms;
  (void)deliver_to_router(&fixture.receiver_endpoint, &fixture.sender_endpoint,
                          &fixture.sender_adapter.router,
                          &fixture.sender_adapter.active_event, now_ms);
  return true;
}

static wl_bulk_sender_result_t drive_transfer(void) {
  for (uint32_t step = 0U; step < DRIVER_STEP_LIMIT;
       ++step, ++fixture.now_ms) {
    wl_bulk_sender_action_t action;
    wl_bulk_sender_result_t result;
    wl_bulk_err_t acquire_result;
    const wl_time_ms_t now_ms = fixture.now_ms;

    zassert_equal(wl_bulk_sender_poll(&fixture.sender, now_ms), WL_BULK_OK);
    zassert_equal(wl_bulk_sender_get_result(&fixture.sender, &result),
                  WL_BULK_OK);
    if (result.state == WL_BULK_SENDER_COMPLETED ||
        result.state == WL_BULK_SENDER_FAILED ||
        result.state == WL_BULK_SENDER_ABORTED) {
      return result;
    }

    acquire_result = wl_bulk_sender_action_acquire(&fixture.sender, &action);
    if (acquire_result == WL_BULK_ERR_NOT_FOUND) {
      continue;
    }
    zassert_equal(acquire_result, WL_BULK_OK);
    send_action(&action, now_ms);
    (void)publish_receiver_status(now_ms);
  }

  zassert_unreachable("bulk transfer driver exceeded its step limit");
  return (wl_bulk_sender_result_t){0};
}

static wl_bulk_descriptor_t
descriptor(uint32_t transfer_id, uint64_t total_length, uint32_t object_crc) {
  return (wl_bulk_descriptor_t){
      .transfer_id = transfer_id,
      .total_length = total_length,
      .requested_chunk_size = CHUNK_SIZE,
      .object_crc32c = object_crc,
  };
}

ZTEST(wirelink_bulk_transfer,
      test_one_megabyte_resume_retry_and_borrowed_chunk_lifetime) {
  const wl_bulk_descriptor_t transfer = descriptor(
      UINT32_C(0x10203040), OBJECT_LENGTH, object_crc32c(OBJECT_LENGTH));
  wl_bulk_sender_result_t result;
  wl_bulk_sender_stats_t sender_stats;
  wl_bulk_receiver_stats_t receiver_stats;
  wl_bulk_receiver_state_t receiver_state;
  uint64_t next_offset;

  transfer_init(RESUME_OFFSET);
  fixture.drop_first_chunk_status = true;
  zassert_equal(wl_bulk_sender_start(&fixture.sender, &transfer), WL_BULK_OK);
  result = drive_transfer();

  zassert_equal(result.state, WL_BULK_SENDER_COMPLETED);
  zassert_equal(result.status, WL_BULK_STATUS_OK);
  zassert_equal(result.next_offset, OBJECT_LENGTH);
  zassert_true(fixture.status_was_dropped);
  zassert_equal(fixture.sink.begin_calls, 1U);
  zassert_equal(fixture.sink.write_calls,
                (OBJECT_LENGTH - RESUME_OFFSET) / CHUNK_SIZE);
  zassert_equal(fixture.sink.finish_calls, 1U);
  zassert_equal(fixture.sink.commit_calls, 1U);
  zassert_false(fixture.sink.invalid_data);
  zassert_true(fixture.sink.observed_borrowed_chunk);
  zassert_equal(fixture.sink.next_offset, OBJECT_LENGTH);

  zassert_equal(wl_bulk_sender_get_stats(&fixture.sender, &sender_stats),
                WL_BULK_OK);
  zassert_equal(sender_stats.retries, 1U);
  zassert_equal(sender_stats.completed, 1U);
  zassert_equal(wl_bulk_receiver_get_stats(&fixture.receiver, &receiver_stats),
                WL_BULK_OK);
  zassert_equal(receiver_stats.duplicate_messages, 1U);
  zassert_equal(receiver_stats.bytes_written,
                (uint32_t)(OBJECT_LENGTH - RESUME_OFFSET));
  zassert_equal(wl_bulk_receiver_get_state(&fixture.receiver, &receiver_state,
                                           &next_offset),
                WL_BULK_OK);
  zassert_equal(receiver_state, WL_BULK_RECEIVER_COMPLETED);
  zassert_equal(next_offset, OBJECT_LENGTH);
}

ZTEST(wirelink_bulk_transfer, test_bad_object_crc_never_commits_sink) {
  const uint64_t length = UINT64_C(4) * CHUNK_SIZE;
  const wl_bulk_descriptor_t transfer =
      descriptor(UINT32_C(0x55667788), length, object_crc32c(length) ^ 1U);
  wl_bulk_sender_result_t result;
  wl_bulk_receiver_state_t receiver_state;
  uint64_t next_offset;

  transfer_init(0U);
  zassert_equal(wl_bulk_sender_start(&fixture.sender, &transfer), WL_BULK_OK);
  result = drive_transfer();

  zassert_equal(result.state, WL_BULK_SENDER_FAILED);
  zassert_equal(result.status, WL_BULK_STATUS_INTEGRITY_FAILED);
  zassert_equal(fixture.sink.finish_calls, 1U);
  zassert_equal(fixture.sink.commit_calls, 0U);
  zassert_equal(wl_bulk_receiver_get_state(&fixture.receiver, &receiver_state,
                                           &next_offset),
                WL_BULK_OK);
  zassert_equal(receiver_state, WL_BULK_RECEIVER_FAILED);
  zassert_equal(next_offset, length);

  zassert_equal(wl_bulk_sender_request_abort(&fixture.sender, 91), WL_BULK_OK);
  result = drive_transfer();
  zassert_equal(result.state, WL_BULK_SENDER_ABORTED);
  zassert_equal(result.status, WL_BULK_STATUS_ABORTED);
  zassert_equal(fixture.sink.abort_calls, 1U);
}

ZTEST(wirelink_bulk_transfer,
      test_abort_before_begin_tombstones_delayed_begin) {
  const uint64_t length = UINT64_C(4) * CHUNK_SIZE;
  const wl_bulk_descriptor_t transfer =
      descriptor(UINT32_C(0xABCDEF01), length, object_crc32c(length));
  wl_bulk_sender_result_t result;
  wl_bulk_receiver_status_view_t view;
  bulk_begin_t delayed_begin;
  control_send_result_t send_result;

  transfer_init(0U);
  zassert_equal(wl_bulk_sender_start(&fixture.sender, &transfer), WL_BULK_OK);
  zassert_equal(wl_bulk_sender_request_abort(&fixture.sender, 77), WL_BULK_OK);
  result = drive_transfer();
  zassert_equal(result.state, WL_BULK_SENDER_ABORTED);
  zassert_equal(result.status, WL_BULK_STATUS_ABORTED);
  zassert_equal(fixture.sink.begin_calls, 0U);
  zassert_equal(fixture.sink.abort_calls, 0U);

  delayed_begin = (bulk_begin_t){
      .has_transfer_id = true,
      .transfer_id = transfer.transfer_id,
      .has_total_length = true,
      .total_length = transfer.total_length,
      .has_requested_chunk_size = true,
      .requested_chunk_size = transfer.requested_chunk_size,
      .has_object_crc32c = true,
      .object_crc32c = transfer.object_crc32c,
  };
  send_result = control_bulk_begin_send(&fixture.sender_endpoint.ctx,
                                        &delayed_begin,
                                        WL_DELIVERY_UNRELIABLE);
  assert_send_ok(send_result);
  drain_tx_success(&fixture.sender_endpoint, 20U);
  fixture.receiver_adapter.now_ms = 20U;
  (void)deliver_to_router(&fixture.sender_endpoint, &fixture.receiver_endpoint,
                          &fixture.receiver_adapter.router,
                          &fixture.receiver_adapter.active_event, 20U);
  zassert_equal(wl_bulk_receiver_status_acquire(&fixture.receiver, &view),
                WL_BULK_OK);
  zassert_equal(view.status.transfer_id, transfer.transfer_id);
  zassert_equal(view.status.phase, WL_BULK_PHASE_BEGIN);
  zassert_equal(view.status.code, WL_BULK_STATUS_CONFLICT);
  zassert_equal(view.status.next_offset, 0U);
  zassert_equal(fixture.sink.begin_calls, 0U);
  zassert_equal(wl_bulk_receiver_status_release(&fixture.receiver, &view),
                WL_BULK_OK);
}

ZTEST_SUITE(wirelink_bulk_transfer, NULL, NULL, NULL, NULL, NULL);
