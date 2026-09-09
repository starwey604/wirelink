/* SPDX-License-Identifier: Apache-2.0 */
/* Deterministic executable specification: one link owner, static composition,
 * real bulk state machines, RAM sink only. No flash writes or actuators. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "product_advanced.h"
#include "wirelink/bulk.h"
#include "wirelink/crc.h"
#include "wirelink/loopback.h"
#include "../../tests/support/test_environment.h"
#ifdef __ZEPHYR__
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#define REPORT(...) printk(__VA_ARGS__)
#else
#define REPORT(...) printf(__VA_ARGS__)
#endif

static product_endpoint_t host, device;
static wl_loopback_t cable;
static wl_bulk_receiver_t receiver;
static wl_bulk_sender_t sender;
static uint8_t source[4096], destination[4096];
static wl_time_ms_t tick;
static uint64_t persisted;
static unsigned commits, aborts, queries, reboot_acks, status_drops, busy_writes;
static bool busy_once = true, drop_status_once = true;
static wl_time_ms_t now(void *context) { (void)context; return tick; }

static wl_bulk_sink_result_t begin(void *context, const wl_bulk_descriptor_t *object,
                                  uint64_t *resume) {
  (void)context;
  assert(object->total_length == sizeof(destination));
  persisted = 0U;
  *resume = 0U;
  return WL_BULK_SINK_OK;
}
static wl_bulk_sink_result_t write_chunk(void *context, uint32_t id, uint64_t offset,
                                        const uint8_t *data, size_t length) {
  (void)context; (void)id;
  if (busy_once) { busy_once = false; ++busy_writes; return WL_BULK_SINK_BUSY; }
  assert(offset == persisted && offset + length <= sizeof(destination));
  memcpy(destination + (size_t)offset, data, length);
  persisted += length;
  return WL_BULK_SINK_OK;
}
static wl_bulk_sink_result_t finish(void *context, const wl_bulk_descriptor_t *object) {
  (void)context;
  assert(persisted == object->total_length);
  assert(wl_crc32c(destination, sizeof(destination)) == object->object_crc32c);
  ++commits;
  return WL_BULK_SINK_OK;
}
static void abort_sink(void *context, uint32_t id, int32_t reason) {
  (void)context; (void)id; (void)reason; ++aborts;
}
static int32_t command(void *context, const bulk_command_t *message, wl_delivery_t delivery) {
  (void)context;
  assert(delivery == WL_DELIVERY_RELIABLE);
  switch (message->phase) {
    case WL_BULK_PHASE_BEGIN: {
      const wl_bulk_descriptor_t object = {message->transfer_id, message->total_length,
          message->chunk_size, message->crc32c};
      return wl_bulk_receiver_on_begin(&receiver, &object, tick);
    }
    case WL_BULK_PHASE_CHUNK: {
      const wl_bulk_chunk_t chunk = {message->transfer_id, message->offset,
          message->data.data, message->data.length};
      return wl_bulk_receiver_on_chunk(&receiver, &chunk, tick);
    }
    case WL_BULK_PHASE_END:
      return wl_bulk_receiver_on_end(&receiver, message->transfer_id,
          message->total_length, message->crc32c, tick);
    case WL_BULK_PHASE_ABORT:
      return wl_bulk_receiver_on_abort(&receiver, message->transfer_id, 0, tick);
    default: return WL_BULK_ERR_PROTOCOL;
  }
}
static int32_t status(void *context, const bulk_status_t *message, wl_delivery_t delivery) {
  (void)context; (void)delivery;
  const wl_bulk_status_t value = {message->transfer_id, (wl_bulk_phase_t)message->phase,
      message->code, message->next_offset, message->chunk_size};
  return wl_bulk_sender_on_status(&sender, &value, tick);
}
static uint8_t receive_progress(void *context, wl_ctx_t *link, wl_time_ms_t time) {
  (void)context; (void)link;
  assert(wl_bulk_receiver_poll(&receiver, time) == WL_BULK_OK);
  wl_bulk_receiver_status_view_t view;
  if (wl_bulk_receiver_status_acquire(&receiver, &view) != WL_BULK_OK) return 0U;
  if (drop_status_once && view.status.phase == WL_BULK_PHASE_CHUNK &&
      view.status.code == WL_BULK_STATUS_OK) {
    drop_status_once = false; ++status_drops;
    assert(wl_bulk_receiver_status_release(&receiver, &view) == WL_BULK_OK);
    return 0U; /* Force a real application-status timeout and duplicate chunk. */
  }
  bulk_status_t message;
  bulk_status_clear(&message);
  message.has_transfer_id = message.has_phase = message.has_code = true;
  message.has_next_offset = message.has_chunk_size = true;
  message.transfer_id = view.status.transfer_id; message.phase = (uint32_t)view.status.phase;
  message.code = view.status.code; message.next_offset = view.status.next_offset;
  message.chunk_size = view.status.accepted_chunk_size;
  product_send_result_t sent = product_endpoint_send_bulk_status(&device, &message);
  if (sent.domain == PRODUCT_SEND_OK)
    assert(wl_bulk_receiver_status_release(&receiver, &view) == WL_BULK_OK);
  else
    assert(wl_bulk_receiver_status_defer(&receiver, &view) == WL_BULK_OK);
  return 0U; /* Pending physical TX wakes the owner; deadline covers retry. */
}
static uint8_t send_progress(void *context, wl_ctx_t *link, wl_time_ms_t time) {
  (void)context; (void)link;
  assert(wl_bulk_sender_poll(&sender, time) == WL_BULK_OK);
  wl_bulk_sender_action_t action;
  if (wl_bulk_sender_action_acquire(&sender, &action) != WL_BULK_OK) return 0U;
  bulk_command_t message;
  bulk_command_clear(&message);
  message.has_phase = message.has_transfer_id = message.has_total_length = true;
  message.has_chunk_size = message.has_crc32c = message.has_offset = message.has_data = true;
  message.phase = (uint32_t)action.phase; message.transfer_id = action.descriptor.transfer_id;
  message.total_length = action.descriptor.total_length;
  message.chunk_size = action.descriptor.requested_chunk_size;
  message.crc32c = action.descriptor.object_crc32c; message.offset = action.offset;
  if (action.phase == WL_BULK_PHASE_CHUNK) {
    assert(action.offset + action.length <= sizeof(source));
    message.data.data = source + (size_t)action.offset; message.data.length = action.length;
  }
  product_send_result_t sent = product_endpoint_send_bulk_command(&host, &message);
  if (sent.domain == PRODUCT_SEND_OK)
    assert(wl_bulk_sender_action_submitted(&sender, &action, time) == WL_BULK_OK);
  else
    assert(wl_bulk_sender_action_defer(&sender, &action) == WL_BULK_OK);
  return 0U;
}
static uint32_t receive_deadline(const void *context, wl_time_ms_t time) {
  (void)context; wl_bulk_deadline_hint_t hint;
  assert(wl_bulk_receiver_get_deadline_hint(&receiver, time, &hint) == WL_BULK_OK);
  return hint.next_deadline_ms;
}
static uint32_t send_deadline(const void *context, wl_time_ms_t time) {
  (void)context; wl_bulk_deadline_hint_t hint;
  assert(wl_bulk_sender_get_deadline_hint(&sender, time, &hint) == WL_BULK_OK);
  return hint.next_deadline_ms;
}
static void peer_session(void *context, wl_ctx_t *link, uint64_t previous,
                         uint64_t session, wl_time_ms_t time) {
  (void)context; (void)time;
  /* The generated RPC runtime exposes the same observation for non-RPC RX.
   * A new session's first message may be BulkCommand, not an RPC request. */
  wl_rpc_peer_observation_t observation;
  assert(product_runtime_peer_observe(link, product_endpoint_runtime(&device),
      session, &observation) == WL_RPC_OK);
  if (previous != 0U) assert(wl_bulk_receiver_reset(&receiver) == WL_BULK_OK);
}
static void close_receiver(void *context, wl_ctx_t *link) {
  (void)context; (void)link;
  assert(wl_bulk_receiver_reset(&receiver) == WL_BULK_OK);
}
static int32_t query(void *context, const query_request_value_t *request,
                     query_response_value_t *response) {
  (void)context; (void)request;
  ++queries; response->has_received = true; response->received = persisted;
  return 0;
}
static int32_t reboot(void *context, const reboot_request_value_t *request,
                      reboot_response_value_t *response) {
  (void)context; (void)request; (void)response;
  assert(commits == 1U);
  return 0; /* Never actually reboot in this test. */
}
static void response_terminal(void *context, const wl_rpc_request_identity_t *identity,
                              int32_t application_status, const wl_event_t *event) {
  (void)context;
  if (identity->request_message_id == REBOOT_REQUEST_MESSAGE_ID &&
      application_status == 0 && event->type == WL_EVT_TX_SUCCESS) ++reboot_acks;
}
static void pass(void) {
  ++tick;
  assert(product_endpoint_step(&host) == WL_OK);
  assert(product_endpoint_step(&device) == WL_OK);
}

int main(void) {
  for (size_t i = 0U; i < sizeof(source); ++i) source[i] = (uint8_t)(i * 17U);
  const wl_bulk_receiver_config_t receiver_config = {sizeof(destination), 256U, 1U, 200U,
    {NULL, begin, write_chunk, finish, abort_sink}};
  const wl_bulk_sender_config_t sender_config = {20U, 3U, 8U};
  assert(wl_bulk_receiver_init(&receiver, &receiver_config) == WL_BULK_OK);
  assert(wl_bulk_sender_init(&sender, &sender_config) == WL_BULK_OK);
  product_endpoint_config_t a, b;
  assert(product_endpoint_config_defaults(&a, test_environment_id(1U, (wl_clock_t){now, NULL})) == WL_OK);
  assert(product_endpoint_config_defaults(&b, test_environment_id(2U, (wl_clock_t){now, NULL})) == WL_OK);
  a.on_bulk_status = status;
  b.on_bulk_command = command; b.on_query = query; b.on_reboot = reboot;
  b.on_response_terminal = response_terminal;
  assert(product_endpoint_init_config(&host, &a) == WL_OK);
  assert(product_endpoint_init_config(&device, &b) == WL_OK);
  const wl_endpoint_service_t host_service = {NULL, send_progress, send_deadline, NULL, NULL};
  const wl_endpoint_service_t device_service = {NULL, receive_progress, receive_deadline, peer_session, close_receiver};
  assert(wl_endpoint_set_services(product_endpoint_handle(&host), &host_service, 1U) == WL_OK);
  assert(wl_endpoint_set_services(product_endpoint_handle(&device), &device_service, 1U) == WL_OK);
  assert(wl_loopback_connect(&cable, product_endpoint_handle(&host), product_endpoint_handle(&device)) == WL_OK);
  wl_bulk_descriptor_t object = {1U, sizeof(source), 256U, wl_crc32c(source, sizeof(source))};
  assert(wl_bulk_sender_start(&sender, &object) == WL_BULK_OK);
  query_request_t query_request; query_request_clear(&query_request);
  product_query_call_t query_call;
  assert(product_endpoint_query_call(&host, &query_request, 100U, &query_call) == WL_RPC_OK);
  wl_bulk_sender_result_t bulk_result;
  unsigned telemetries = 0U;
  for (unsigned i = 0U; i < 1000U; ++i) {
    pass();
    telemetry_t telemetry; telemetry_clear(&telemetry);
    telemetry.has_tick = true; telemetry.tick = tick;
    (void)product_endpoint_send_telemetry(&device, &telemetry);
    if (product_endpoint_read_telemetry(&host, &telemetry) == WL_OK) ++telemetries;
    assert(wl_bulk_sender_get_result(&sender, &bulk_result) == WL_BULK_OK);
    if (bulk_result.state == WL_BULK_SENDER_COMPLETED) break;
  }
  assert(bulk_result.state == WL_BULK_SENDER_COMPLETED);
  assert(commits == 1U && busy_writes == 1U && status_drops == 1U && telemetries > 0U);
  assert(memcmp(source, destination, sizeof(source)) == 0);
  product_query_result_t query_result;
  assert(product_endpoint_query_inspect(&host, &query_call, &query_result) == WL_RPC_OK);
  assert(query_result.response_valid && queries == 1U);
  assert(product_endpoint_query_release(&host, &query_call) == WL_RPC_OK);
  reboot_request_t reboot_request; reboot_request_clear(&reboot_request);
  product_reboot_call_t reboot_call;
  assert(product_endpoint_reboot_call(&host, &reboot_request, 100U, &reboot_call) == WL_RPC_OK);
  for (unsigned i = 0U; i < 100U && reboot_acks == 0U; ++i) pass();
  assert(reboot_acks == 1U);
  product_reboot_result_t reboot_result;
  for (unsigned i = 0U; i < 100U; ++i) {
    assert(product_endpoint_reboot_inspect(&host, &reboot_call, &reboot_result) == WL_RPC_OK);
    if (reboot_result.response_valid) break;
    pass();
  }
  assert(reboot_result.response_valid);
  assert(product_endpoint_reboot_release(&host, &reboot_call) == WL_RPC_OK);
  /* Close during a second active upload must abort the sink exactly once. */
  assert(wl_bulk_sender_reset(&sender) == WL_BULK_OK);
  ++object.transfer_id;
  assert(wl_bulk_sender_start(&sender, &object) == WL_BULK_OK);
  for (unsigned i = 0U; i < 8U; ++i) pass();
  assert(product_endpoint_close(&host) == WL_OK);
  assert(product_endpoint_close(&device) == WL_OK);
  assert(product_endpoint_close(&device) == WL_OK);
  assert(aborts == 1U);
  REPORT("COMPOSED_SERVICES PASS bytes=4096 rpc=%u telemetry=%u busy=%u dropped_status=%u reboot_ack=%u abort=%u\n",
      queries, telemetries, busy_writes, status_drops, reboot_acks, aborts);
  return 0;
}
