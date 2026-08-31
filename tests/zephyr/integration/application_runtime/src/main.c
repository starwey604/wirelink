/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "control_runtime.h"
#include "wirelink/fifo.h"
#include "wirelink/latest.h"
#include "wirelink/rpc.h"

#define APP_MAX_PAYLOAD 256U
#define APP_UNIT_CAPACITY                                                      \
  (WL_FRAME_HEADER_SIZE + APP_MAX_PAYLOAD + WL_FRAME_MAX_CRC)
#define APP_CONTROL_CAPACITY (WL_FRAME_HEADER_SIZE + WL_FRAME_MAX_CRC)
#define JOINT_FIFO_CAPACITY 3U

struct endpoint {
  wl_ctx_t ctx;
  wl_config_t config;
  wl_storage_t storage;
  uint8_t tx_payload[APP_MAX_PAYLOAD];
  uint8_t tx_unit[APP_UNIT_CAPACITY];
  uint8_t control_unit[APP_CONTROL_CAPACITY];
  uint8_t rx_fallback[APP_UNIT_CAPACITY];
  uint8_t outbound[APP_UNIT_CAPACITY];
  size_t outbound_length;
};

struct latest_fixture {
  wl_latest_t mailbox;
  arm_mit_command_t slots[WL_LATEST_SLOT_COUNT];
  control_runtime_t runtime;
};

struct joint_fifo_fixture {
  wl_fifo_t fifo;
  joint_command_t slots[JOINT_FIFO_CAPACITY];
  control_runtime_t runtime;
};

struct rpc_client_fixture {
  wl_rpc_client_t runtime;
  wl_rpc_client_slot_t slots[1];
  uint8_t response_storage[16];
  home_response_t decode_scratch;
  control_router_t router;
  uint32_t responses_handled;
};

struct rpc_server_fixture {
  wl_rpc_server_t runtime;
  wl_rpc_server_pending_slot_t pending[1];
  wl_rpc_server_cache_slot_t cache[1];
  uint8_t response_storage[32];
  home_request_t decode_scratch;
  control_router_t router;
  wl_time_ms_t now_ms;
  wl_rpc_server_disposition_t last_disposition;
  wl_rpc_server_response_t last_replay;
  uint32_t requests_handled;
  uint32_t executions;
  uint32_t pending_duplicates;
  uint32_t replays;
  uint32_t reliable_requests;
};

static struct endpoint endpoint_client;
static struct endpoint endpoint_server;
static struct latest_fixture latest_fixture;
static struct joint_fifo_fixture joint_fifo;
static struct rpc_client_fixture rpc_client;
static struct rpc_server_fixture rpc_server;

static wl_sink_result_t memory_sink(void *user_data, wl_io_token_t token,
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
  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->config = (wl_config_t){
      .max_payload_len = APP_MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_NATIVE_PACKET,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = session_id,
      .max_retries = 1U,
      .ack_timeout_ms = 10U,
      .max_transmission_unit = APP_UNIT_CAPACITY,
  };
  endpoint->storage = (wl_storage_t){
      .tx_payload = endpoint->tx_payload,
      .tx_payload_size = sizeof(endpoint->tx_payload),
      .tx_unit = endpoint->tx_unit,
      .tx_unit_size = sizeof(endpoint->tx_unit),
      .control_unit = endpoint->control_unit,
      .control_unit_size = sizeof(endpoint->control_unit),
      .rx_fallback = endpoint->rx_fallback,
      .rx_fallback_size = sizeof(endpoint->rx_fallback),
  };
  zassert_ok(wl_init(&endpoint->ctx, &endpoint->config, &endpoint->storage));
  zassert_ok(wl_set_sink(&endpoint->ctx, memory_sink, endpoint));
}

static void deliver(struct endpoint *source, struct endpoint *destination) {
  const size_t length = source->outbound_length;

  zassert_not_equal(length, 0U);
  source->outbound_length = 0U;
  zassert_ok(wl_feed_unit(&destination->ctx, source->outbound, length));
}

static wl_event_t poll_event(struct endpoint *endpoint, wl_time_ms_t now_ms,
                             wl_event_type_t expected_type) {
  wl_event_t event = {0};

  zassert_ok(wl_poll(&endpoint->ctx, now_ms, &event));
  zassert_equal(event.type, expected_type);
  return event;
}

static void expect_local_unreliable_completion(struct endpoint *endpoint,
                                               wl_time_ms_t now_ms) {
  const wl_event_t event = poll_event(endpoint, now_ms, WL_EVT_TX_SUCCESS);

  zassert_equal(event.handle, 0U);
}

static control_dispatch_result_t dispatch_next(struct endpoint *endpoint,
                                               control_router_t *router,
                                               wl_time_ms_t now_ms,
                                               wl_event_type_t expected_type,
                                               uint16_t expected_message_id) {
  const wl_event_t event = poll_event(endpoint, now_ms, expected_type);
  control_dispatch_result_t result;

  zassert_equal(event.message_id, expected_message_id);
  result = control_dispatch_event(&endpoint->ctx, &event, router);
  zassert_equal(result.domain, CONTROL_DISPATCH_OK);
  zassert_equal(result.message_id, expected_message_id);
  return result;
}

static void dispatch_latest(struct endpoint *endpoint, wl_time_ms_t now_ms) {
  const wl_event_t event = poll_event(endpoint, now_ms, WL_EVT_UNRELIABLE_RX);
  control_runtime_result_t result;

  zassert_equal(event.message_id, ARM_MIT_COMMAND_MESSAGE_ID);
  result = control_runtime_dispatch_event(&endpoint->ctx, &event,
                                          &latest_fixture.runtime);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(result.message_id, ARM_MIT_COMMAND_MESSAGE_ID);
  zassert_equal(result.storage_result, WL_OK);
  zassert_equal(result.codec_status, WL_CODEC_OK);
}

static void latest_init(void) {
  wl_latest_config_t config;
  wl_latest_requirements_t requirements;
  wl_latest_storage_t storage;

  memset(&latest_fixture, 0, sizeof(latest_fixture));
  config = (wl_latest_config_t){
      .value_size = sizeof(arm_mit_command_t),
      .value_alignment = _Alignof(arm_mit_command_t),
  };
  zassert_ok(wl_latest_requirements(&config, &requirements));
  zassert_equal(requirements.slot_count, WL_LATEST_SLOT_COUNT);
  zassert_true(requirements.storage_size <= sizeof(latest_fixture.slots));
  storage = (wl_latest_storage_t){
      .data = latest_fixture.slots,
      .size = sizeof(latest_fixture.slots),
  };
  zassert_ok(wl_latest_init(&latest_fixture.mailbox, &config, &storage));
  latest_fixture.runtime.arm_mit_command_latest = &latest_fixture.mailbox;
}

ZTEST(wirelink_application_runtime,
      test_typed_latest_stream_coalesces_without_heap) {
  struct endpoint *sender = &endpoint_client;
  struct endpoint *receiver = &endpoint_server;
  arm_mit_command_t command;
  wl_latest_view_t view;
  wl_latest_stats_t stats;

  endpoint_init(sender, UINT64_C(0x1111));
  endpoint_init(receiver, UINT64_C(0x2222));
  latest_init();

  for (uint32_t sequence = 1U; sequence <= 3U; ++sequence) {
    control_send_result_t send_result;

    arm_mit_command_clear(&command);
    command.has_controls = true;
    command.has_sequence = true;
    command.sequence = sequence;
    command.has_dt_s = true;
    command.dt_s = 0.001F;
    for (size_t index = 0U; index < ARRAY_SIZE(command.controls); ++index) {
      command.controls[index] = (float)(sequence * 100U + index);
    }

    send_result = control_arm_mit_command_send_direct(&sender->ctx, &command,
                                                      WL_DELIVERY_UNRELIABLE);
    zassert_equal(send_result.domain, CONTROL_SEND_OK);
    zassert_equal(send_result.core_result, WL_OK);
    zassert_true(send_result.payload_length > 120U);
    expect_local_unreliable_completion(sender, sequence);
    deliver(sender, receiver);
    dispatch_latest(receiver, sequence);
  }

  zassert_ok(wl_latest_read_acquire(&latest_fixture.mailbox, &view));
  zassert_equal(view.generation, 3U);
  zassert_equal(view.value_size, sizeof(arm_mit_command_t));
  const arm_mit_command_t *latest = view.value;
  zassert_true(latest->has_sequence);
  zassert_equal(latest->sequence, 3U);
  zassert_equal(latest->controls[0], 300.0F);
  zassert_equal(latest->controls[29], 329.0F);
  zassert_ok(wl_latest_read_release(&latest_fixture.mailbox, &view));
  zassert_equal(wl_latest_read_acquire(&latest_fixture.mailbox, &view),
                WL_ERR_NO_DATA);

  zassert_ok(wl_latest_get_stats(&latest_fixture.mailbox, &stats));
  zassert_equal(stats.publishes, 3U);
  zassert_equal(stats.reads, 1U);
  zassert_equal(stats.coalesced, 2U);
}

static void joint_fifo_init(void) {
  wl_fifo_config_t config;
  wl_fifo_requirements_t requirements;
  wl_fifo_storage_t storage;

  memset(&joint_fifo, 0, sizeof(joint_fifo));
  config = (wl_fifo_config_t){
      .value_size = sizeof(joint_command_t),
      .value_alignment = _Alignof(joint_command_t),
      .capacity = JOINT_FIFO_CAPACITY,
  };
  zassert_ok(wl_fifo_requirements(&config, &requirements));
  zassert_equal(requirements.slot_count, JOINT_FIFO_CAPACITY);
  zassert_true(requirements.storage_size <= sizeof(joint_fifo.slots));
  storage = (wl_fifo_storage_t){
      .data = joint_fifo.slots,
      .size = sizeof(joint_fifo.slots),
  };
  zassert_ok(wl_fifo_init(&joint_fifo.fifo, &config, &storage));
  joint_fifo.runtime.joint_command_fifo = &joint_fifo.fifo;
}

static control_runtime_result_t send_joint_reliable(struct endpoint *sender,
                                                    struct endpoint *receiver,
                                                    uint32_t sequence,
                                                    wl_time_ms_t now_ms) {
  joint_command_t command;
  control_send_result_t send_result;
  control_runtime_result_t runtime_result;
  wl_tx_result_t tx_result;
  wl_event_t event;

  joint_command_clear(&command);
  command.has_position_bits = true;
  command.position_bits = sequence;
  command.has_velocity_bits = true;
  command.velocity_bits = sequence + UINT32_C(0x1000);
  command.has_torque_bits = true;
  command.torque_bits = sequence + UINT32_C(0x2000);
  command.has_kp_bits = true;
  command.kp_bits = sequence + UINT32_C(0x3000);
  command.has_kd_bits = true;
  command.kd_bits = sequence + UINT32_C(0x4000);
  command.has_mode = true;
  command.mode = MIT;

  send_result = control_joint_command_send_direct(&sender->ctx, &command,
                                                  WL_DELIVERY_RELIABLE);
  zassert_equal(send_result.domain, CONTROL_SEND_OK);
  zassert_not_equal(send_result.handle, 0U);
  deliver(sender, receiver);

  event = poll_event(receiver, now_ms, WL_EVT_RELIABLE_RX);
  zassert_equal(event.message_id, JOINT_COMMAND_MESSAGE_ID);
  runtime_result = control_runtime_dispatch_event(&receiver->ctx, &event,
                                                  &joint_fifo.runtime);
  zassert_equal(runtime_result.message_id, JOINT_COMMAND_MESSAGE_ID);
  zassert_equal(runtime_result.event_type, WL_EVT_RELIABLE_RX);

  /* Dispatch owns and releases the RX lease; the link ACK is independent. */
  deliver(receiver, sender);
  event = poll_event(sender, now_ms + 1U, WL_EVT_TX_SUCCESS);
  zassert_equal(event.handle, send_result.handle);
  zassert_equal(wl_tx_take(&sender->ctx, event.handle, &tx_result), WL_OK);
  zassert_equal(tx_result.state, WL_TX_STATE_SUCCESS);
  return runtime_result;
}

ZTEST(wirelink_application_runtime,
      test_generated_fifo_preserves_order_and_releases_full_event) {
  struct endpoint *sender = &endpoint_client;
  struct endpoint *receiver = &endpoint_server;
  control_runtime_result_t result;
  wl_fifo_view_t view;
  wl_fifo_stats_t stats;

  endpoint_init(sender, UINT64_C(0x5151));
  endpoint_init(receiver, UINT64_C(0x5252));
  joint_fifo_init();

  for (uint32_t sequence = 1U; sequence <= JOINT_FIFO_CAPACITY; ++sequence) {
    result = send_joint_reliable(sender, receiver, sequence, sequence * 2U);
    zassert_equal(result.domain, CONTROL_RUNTIME_OK);
    zassert_equal(result.storage_result, WL_OK);
    zassert_equal(result.codec_status, WL_CODEC_OK);
  }

  result = send_joint_reliable(sender, receiver, 4U, 8U);
  zassert_equal(result.domain, CONTROL_RUNTIME_STORAGE_ERROR);
  zassert_equal(result.storage_result, WL_ERR_QUEUE_FULL);

  for (uint32_t sequence = 1U; sequence <= JOINT_FIFO_CAPACITY; ++sequence) {
    const joint_command_t *command;

    zassert_ok(wl_fifo_read_acquire(&joint_fifo.fifo, &view));
    zassert_equal(view.value_size, sizeof(joint_command_t));
    command = view.value;
    zassert_true(command->has_position_bits);
    zassert_equal(command->position_bits, sequence);
    zassert_true(command->has_velocity_bits);
    zassert_equal(command->velocity_bits, sequence + UINT32_C(0x1000));
    zassert_true(command->has_torque_bits);
    zassert_equal(command->torque_bits, sequence + UINT32_C(0x2000));
    zassert_true(command->has_kp_bits);
    zassert_equal(command->kp_bits, sequence + UINT32_C(0x3000));
    zassert_true(command->has_kd_bits);
    zassert_equal(command->kd_bits, sequence + UINT32_C(0x4000));
    zassert_true(command->has_mode);
    zassert_equal(command->mode, MIT);
    zassert_ok(wl_fifo_read_release(&joint_fifo.fifo, &view));
  }
  zassert_equal(wl_fifo_read_acquire(&joint_fifo.fifo, &view), WL_ERR_NO_DATA);

  /* A frame after the full-queue path proves its RX lease was released once. */
  result = send_joint_reliable(sender, receiver, 5U, 10U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_ok(wl_fifo_read_acquire(&joint_fifo.fifo, &view));
  const joint_command_t *last = view.value;
  zassert_equal(last->position_bits, 5U);
  zassert_ok(wl_fifo_read_release(&joint_fifo.fifo, &view));

  zassert_ok(wl_fifo_get_stats(&joint_fifo.fifo, &stats));
  zassert_equal(stats.depth, 0U);
  zassert_equal(stats.high_watermark, JOINT_FIFO_CAPACITY);
  zassert_equal(stats.publishes, 4U);
  zassert_equal(stats.consumes, 4U);
  zassert_equal(stats.full_rejections, 1U);
  zassert_equal(stats.errors, 0U);
}

static uint64_t home_request_fingerprint(const home_request_t *request) {
  uint64_t fingerprint = UINT64_C(1469598103934665603);

  fingerprint ^= request->operation_id;
  fingerprint *= UINT64_C(1099511628211);
  fingerprint ^= request->joint_mask;
  fingerprint *= UINT64_C(1099511628211);
  return fingerprint;
}

static int32_t route_home_request(void *user_data,
                                  const home_request_t *message,
                                  wl_delivery_t delivery) {
  struct rpc_server_fixture *fixture = user_data;
  wl_rpc_request_identity_t identity;
  wl_rpc_server_response_t replay;
  wl_rpc_server_disposition_t disposition;
  wl_rpc_err_t error;

  if (!message->has_operation_id || message->operation_id == 0U ||
      !message->has_joint_mask) {
    return WL_RPC_ERR_INVALID_ARG;
  }
  identity = (wl_rpc_request_identity_t){
      .operation_id = message->operation_id,
      .request_message_id = HOME_REQUEST_MESSAGE_ID,
      .response_message_id = HOME_RESPONSE_MESSAGE_ID,
      .request_fingerprint = home_request_fingerprint(message),
  };
  error = wl_rpc_server_begin(&fixture->runtime, &identity, fixture->now_ms,
                              &disposition, &replay);
  if (error != WL_RPC_OK) {
    return error;
  }

  ++fixture->requests_handled;
  fixture->last_disposition = disposition;
  fixture->last_replay = replay;
  if (delivery == WL_DELIVERY_RELIABLE) {
    ++fixture->reliable_requests;
  }
  switch (disposition) {
  case WL_RPC_SERVER_NEW:
    ++fixture->executions;
    break;
  case WL_RPC_SERVER_PENDING_DUPLICATE:
    ++fixture->pending_duplicates;
    break;
  case WL_RPC_SERVER_REPLAY:
    ++fixture->replays;
    break;
  default:
    return WL_RPC_ERR_OPERATION_CONFLICT;
  }
  return 0;
}

static int32_t route_home_response(void *user_data,
                                   const home_response_t *message,
                                   wl_delivery_t delivery) {
  struct rpc_client_fixture *fixture = user_data;
  wl_rpc_err_t error;

  if (delivery != WL_DELIVERY_RELIABLE || !message->has_operation_id ||
      message->operation_id == 0U || !message->has_status) {
    return WL_RPC_ERR_INVALID_ARG;
  }
  error = wl_rpc_client_on_response(&fixture->runtime, HOME_RESPONSE_MESSAGE_ID,
                                    message->operation_id, message->status,
                                    NULL, 0U);
  if (error == WL_RPC_OK) {
    ++fixture->responses_handled;
  }
  return error;
}

static void rpc_init(void) {
  wl_rpc_client_config_t client_config;
  wl_rpc_server_config_t server_config;

  memset(&rpc_client, 0, sizeof(rpc_client));
  memset(&rpc_server, 0, sizeof(rpc_server));
  client_config = (wl_rpc_client_config_t){
      .slots = rpc_client.slots,
      .slot_count = ARRAY_SIZE(rpc_client.slots),
      .response_storage = rpc_client.response_storage,
      .response_storage_size = sizeof(rpc_client.response_storage),
      .response_capacity_per_slot = sizeof(rpc_client.response_storage),
      .next_operation_id = 1U,
  };
  server_config = (wl_rpc_server_config_t){
      .pending_slots = rpc_server.pending,
      .pending_slot_count = ARRAY_SIZE(rpc_server.pending),
      .cache_slots = rpc_server.cache,
      .cache_slot_count = ARRAY_SIZE(rpc_server.cache),
      .response_storage = rpc_server.response_storage,
      .response_storage_size = sizeof(rpc_server.response_storage),
      .response_capacity_per_slot = sizeof(rpc_server.response_storage),
      .cache_policy = WL_RPC_CACHE_REJECT_NEW,
  };
  zassert_equal(wl_rpc_client_init(&rpc_client.runtime, &client_config),
                WL_RPC_OK);
  zassert_equal(wl_rpc_server_init(&rpc_server.runtime, &server_config),
                WL_RPC_OK);

  rpc_client.router.home_response = (control_home_response_route_t){
      .scratch = &rpc_client.decode_scratch,
      .handler = route_home_response,
      .user_data = &rpc_client,
  };
  rpc_server.router.home_request = (control_home_request_route_t){
      .scratch = &rpc_server.decode_scratch,
      .handler = route_home_request,
      .user_data = &rpc_server,
  };
}

static void send_unreliable_request(struct endpoint *client,
                                    struct endpoint *server,
                                    const home_request_t *request,
                                    wl_time_ms_t now_ms,
                                    wl_rpc_server_disposition_t expected) {
  const control_send_result_t send_result = control_home_request_send_direct(
      &client->ctx, request, WL_DELIVERY_UNRELIABLE);

  zassert_equal(send_result.domain, CONTROL_SEND_OK);
  expect_local_unreliable_completion(client, now_ms);
  deliver(client, server);
  rpc_server.now_ms = now_ms;
  (void)dispatch_next(server, &rpc_server.router, now_ms, WL_EVT_UNRELIABLE_RX,
                      HOME_REQUEST_MESSAGE_ID);
  zassert_equal(rpc_server.last_disposition, expected);
}

ZTEST(wirelink_application_runtime,
      test_typed_rpc_separates_link_ack_and_application_completion) {
  struct endpoint *client = &endpoint_client;
  struct endpoint *server = &endpoint_server;
  home_request_t request;
  home_response_t response;
  home_response_t replay_decoded;
  control_send_result_t send_result;
  wl_rpc_client_result_t client_result;
  wl_rpc_server_response_t completed;
  wl_tx_result_t tx_result;
  uint8_t cached_payload[16];
  size_t cached_payload_length = 0U;
  uint32_t operation_id = 0U;

  endpoint_init(client, UINT64_C(0x3333));
  endpoint_init(server, UINT64_C(0x4444));
  rpc_init();

  zassert_equal(
      wl_rpc_client_begin(&rpc_client.runtime, HOME_REQUEST_MESSAGE_ID,
                          HOME_RESPONSE_MESSAGE_ID, 100U, 0U, &operation_id),
      WL_RPC_OK);
  zassert_not_equal(operation_id, 0U);
  home_request_clear(&request);
  request.has_operation_id = true;
  request.operation_id = operation_id;
  request.has_joint_mask = true;
  request.joint_mask = 0x3FU;

  send_result = control_home_request_send_direct(&client->ctx, &request,
                                                 WL_DELIVERY_RELIABLE);
  zassert_equal(send_result.domain, CONTROL_SEND_OK);
  zassert_not_equal(send_result.handle, 0U);
  zassert_equal(wl_rpc_client_bind_tx(&rpc_client.runtime, operation_id,
                                      send_result.handle),
                WL_RPC_OK);
  zassert_equal(
      wl_rpc_client_get(&rpc_client.runtime, operation_id, &client_result),
      WL_RPC_OK);
  zassert_equal(client_result.state, WL_RPC_CLIENT_LINK_PENDING);

  deliver(client, server);
  rpc_server.now_ms = 1U;
  (void)dispatch_next(server, &rpc_server.router, 1U, WL_EVT_RELIABLE_RX,
                      HOME_REQUEST_MESSAGE_ID);
  zassert_equal(rpc_server.last_disposition, WL_RPC_SERVER_NEW);
  zassert_equal(rpc_server.executions, 1U);
  zassert_equal(rpc_server.reliable_requests, 1U);

  /* Server application work is pending and the transport ACK is separate. */
  zassert_equal(
      wl_rpc_client_get(&rpc_client.runtime, operation_id, &client_result),
      WL_RPC_OK);
  zassert_equal(client_result.state, WL_RPC_CLIENT_LINK_PENDING);
  deliver(server, client);
  const wl_event_t request_tx = poll_event(client, 2U, WL_EVT_TX_SUCCESS);
  zassert_equal(request_tx.handle, send_result.handle);
  zassert_equal(wl_rpc_client_on_tx_event(&rpc_client.runtime, &request_tx),
                WL_RPC_OK);
  zassert_equal(wl_tx_take(&client->ctx, request_tx.handle, &tx_result), WL_OK);
  zassert_equal(tx_result.state, WL_TX_STATE_SUCCESS);
  zassert_equal(
      wl_rpc_client_get(&rpc_client.runtime, operation_id, &client_result),
      WL_RPC_OK);
  zassert_equal(client_result.state, WL_RPC_CLIENT_WAIT_RESPONSE);
  zassert_equal(client_result.link_delivery_confirmed, 1U);

  /* A new link packet with the same operation remains one application job. */
  send_unreliable_request(client, server, &request, 3U,
                          WL_RPC_SERVER_PENDING_DUPLICATE);
  zassert_equal(rpc_server.executions, 1U);
  zassert_equal(rpc_server.pending_duplicates, 1U);

  home_response_clear(&response);
  response.has_operation_id = true;
  response.operation_id = operation_id;
  response.has_status = true;
  response.status = OPERATION_OK;
  zassert_equal(home_response_encode(&response, cached_payload,
                                     sizeof(cached_payload),
                                     &cached_payload_length),
                WL_CODEC_OK);
  zassert_equal(wl_rpc_server_complete(&rpc_server.runtime, operation_id,
                                       OPERATION_OK, cached_payload,
                                       cached_payload_length, 4U, &completed),
                WL_RPC_OK);
  zassert_equal(completed.identity.operation_id, operation_id);
  zassert_equal(completed.response_length, cached_payload_length);
  zassert_mem_equal(completed.response_data, cached_payload,
                    cached_payload_length);

  send_result = control_home_response_send_direct(&server->ctx, &response,
                                                  WL_DELIVERY_RELIABLE);
  zassert_equal(send_result.domain, CONTROL_SEND_OK);
  zassert_equal(send_result.payload_length, cached_payload_length);
  deliver(server, client);
  (void)dispatch_next(client, &rpc_client.router, 5U, WL_EVT_RELIABLE_RX,
                      HOME_RESPONSE_MESSAGE_ID);
  zassert_equal(rpc_client.responses_handled, 1U);
  zassert_equal(
      wl_rpc_client_get(&rpc_client.runtime, operation_id, &client_result),
      WL_RPC_OK);
  zassert_equal(client_result.state, WL_RPC_CLIENT_COMPLETED);
  zassert_equal(client_result.application_status, OPERATION_OK);
  zassert_equal(client_result.link_delivery_confirmed, 1U);

  deliver(client, server);
  const wl_event_t response_tx = poll_event(server, 6U, WL_EVT_TX_SUCCESS);
  zassert_equal(response_tx.handle, send_result.handle);
  zassert_equal(wl_tx_take(&server->ctx, response_tx.handle, &tx_result),
                WL_OK);

  send_unreliable_request(client, server, &request, 7U, WL_RPC_SERVER_REPLAY);
  zassert_equal(rpc_server.executions, 1U);
  zassert_equal(rpc_server.replays, 1U);
  zassert_equal(rpc_server.last_replay.application_status, OPERATION_OK);
  zassert_equal(rpc_server.last_replay.response_length, cached_payload_length);
  zassert_mem_equal(rpc_server.last_replay.response_data, cached_payload,
                    cached_payload_length);
  zassert_equal(home_response_decode(rpc_server.last_replay.response_data,
                                     rpc_server.last_replay.response_length,
                                     &replay_decoded),
                WL_CODEC_OK);
  zassert_true(replay_decoded.has_operation_id);
  zassert_equal(replay_decoded.operation_id, operation_id);
  zassert_true(replay_decoded.has_status);
  zassert_equal(replay_decoded.status, OPERATION_OK);

  zassert_equal(rpc_server.router.counters.delivered, 3U);
  zassert_equal(rpc_client.router.counters.delivered, 1U);
  zassert_equal(wl_rpc_client_release(&rpc_client.runtime, operation_id),
                WL_RPC_OK);
}

ZTEST_SUITE(wirelink_application_runtime, NULL, NULL, NULL, NULL, NULL);
