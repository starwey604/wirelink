/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "control_runtime.h"
#include "wirelink/fifo.h"
#include "wirelink/frame.h"
#include "wirelink/latest.h"
#include "wirelink/port.h"
#include "wirelink/pump.h"
#include "wirelink/rpc.h"

#define APP_MAX_PAYLOAD 256U
#define APP_UNIT_CAPACITY                                                      \
  (WL_FRAME_HEADER_SIZE + APP_MAX_PAYLOAD + WL_FRAME_MAX_CRC)
#define APP_CONTROL_CAPACITY (WL_FRAME_HEADER_SIZE + WL_FRAME_MAX_CRC)
#define JOINT_FIFO_CAPACITY 3U
#define RUNTIME_STORAGE_CAPACITY 2048U

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

typedef union runtime_storage_buffer {
  max_align_t align;
  uint8_t bytes[RUNTIME_STORAGE_CAPACITY];
} runtime_storage_buffer_t;

struct latest_fixture {
  control_runtime_instance_t instance;
  runtime_storage_buffer_t storage;
};

struct joint_fifo_fixture {
  control_runtime_instance_t instance;
  runtime_storage_buffer_t storage;
};

struct rpc_client_fixture {
  control_runtime_instance_t instance;
  runtime_storage_buffer_t storage;
};

struct rpc_server_fixture {
  control_runtime_instance_t instance;
  runtime_storage_buffer_t storage;
  uint32_t handler_calls;
  uint32_t last_operation_id;
  uint32_t last_joint_mask;
  wl_rpc_server_request_t last_request;
  wl_delivery_t last_delivery;
  int32_t handler_result;
};

static struct endpoint endpoint_client;
static struct endpoint endpoint_server;
static struct latest_fixture latest_fixture;
static struct joint_fifo_fixture joint_fifo;
static struct rpc_client_fixture rpc_client;
static struct rpc_server_fixture rpc_server;
static control_runtime_instance_t requirements_instance;
static runtime_storage_buffer_t requirements_storage;

static const control_runtime_retained_detail_t *
retained_detail(const control_runtime_result_t *result) {
  zassert_equal(result->detail_kind, CONTROL_RUNTIME_DETAIL_RETAINED);
  return &result->detail.retained;
}

static const control_runtime_rpc_detail_t *
rpc_detail(const control_runtime_result_t *result) {
  zassert_equal(result->detail_kind, CONTROL_RUNTIME_DETAIL_RPC);
  return &result->detail.rpc;
}

static control_runtime_config_t retained_runtime_config(uint32_t capacity) {
  return (control_runtime_config_t){
      .joint_command_fifo_capacity = capacity,
      .arm_mit_command_latest_initial_generation = 0U,
      .rpc_server_cache_policy = WL_RPC_CACHE_REJECT_NEW,
  };
}

static control_runtime_requirements_t
initialize_generated_runtime(control_runtime_instance_t *instance,
                             runtime_storage_buffer_t *buffer,
                             const control_runtime_config_t *config) {
  control_runtime_requirements_t requirements;
  control_runtime_storage_t storage;

  zassert_ok(control_runtime_requirements(config, &requirements));
  zassert_true(requirements.storage_size <= sizeof(buffer->bytes));
  zassert_true(requirements.storage_alignment <= _Alignof(*buffer));
  storage = (control_runtime_storage_t){
      .data = buffer->bytes,
      .size = requirements.storage_size,
  };
  zassert_ok(control_runtime_init(instance, config, &storage));
  zassert_equal(instance->runtime.joint_command_fifo,
                &instance->joint_command_fifo);
  zassert_equal(instance->runtime.arm_mit_command_latest,
                &instance->arm_mit_command_latest);
  return requirements;
}

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

ZTEST(wirelink_application_runtime,
      test_generated_runtime_requirements_and_storage_contract) {
  control_runtime_config_t config = retained_runtime_config(2U);
  control_runtime_requirements_t requirements;
  control_runtime_storage_t storage;

  config.rpc_client_enabled = 1U;
  config.rpc_client_slot_count = 2U;
  config.rpc_client_response_capacity = 16U;
  config.rpc_client_next_operation_id = 7U;
  config.rpc_server_enabled = 1U;
  config.rpc_server_pending_slot_count = 2U;
  config.rpc_server_cache_slot_count = 2U;
  config.rpc_server_response_capacity = 32U;
  config.home_canonical_request_capacity = 32U;

  memset(&requirements_instance, 0xA5, sizeof(requirements_instance));
  memset(&requirements_storage, 0, sizeof(requirements_storage));
  zassert_ok(control_runtime_requirements(&config, &requirements));
  zassert_true(requirements.storage_size > 1U);
  zassert_true(requirements.storage_size < sizeof(requirements_storage.bytes));
  zassert_true(requirements.storage_alignment > 1U);
  zassert_true(requirements.storage_alignment <=
               _Alignof(runtime_storage_buffer_t));

  storage = (control_runtime_storage_t){
      .data = requirements_storage.bytes,
      .size = requirements.storage_size - 1U,
  };
  zassert_equal(control_runtime_init(&requirements_instance, &config, &storage),
                WL_ERR_BUF_TOO_SMALL);

  storage = (control_runtime_storage_t){
      .data = requirements_storage.bytes + 1U,
      .size = requirements.storage_size,
  };
  zassert_equal(control_runtime_init(&requirements_instance, &config, &storage),
                WL_ERR_INVALID_ARG);

  storage = (control_runtime_storage_t){
      .data = requirements_storage.bytes,
      .size = requirements.storage_size,
  };
  zassert_ok(control_runtime_init(&requirements_instance, &config, &storage));
  zassert_equal(requirements_instance.runtime.joint_command_fifo,
                &requirements_instance.joint_command_fifo);
  zassert_equal(requirements_instance.runtime.arm_mit_command_latest,
                &requirements_instance.arm_mit_command_latest);
  zassert_equal(requirements_instance.runtime.rpc_client,
                &requirements_instance.rpc_client);
  zassert_equal(requirements_instance.runtime.rpc_server,
                &requirements_instance.rpc_server);
}

static void dispatch_latest(struct endpoint *endpoint, wl_time_ms_t now_ms) {
  const wl_event_t event = poll_event(endpoint, now_ms, WL_EVT_UNRELIABLE_RX);
  control_runtime_result_t result;

  zassert_equal(event.message_id, ARM_MIT_COMMAND_MESSAGE_ID);
  result = control_runtime_dispatch_event(
      &endpoint->ctx, &event, &latest_fixture.instance.runtime, now_ms);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(result.message_id, ARM_MIT_COMMAND_MESSAGE_ID);
  zassert_equal(retained_detail(&result)->storage_result, WL_OK);
  zassert_equal(retained_detail(&result)->codec_status, WL_CODEC_OK);
}

static void latest_init(void) {
  control_runtime_config_t config;

  memset(&latest_fixture, 0, sizeof(latest_fixture));
  config = retained_runtime_config(1U);
  (void)initialize_generated_runtime(&latest_fixture.instance,
                                     &latest_fixture.storage, &config);
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

    send_result = control_arm_mit_command_send(&sender->ctx, &command,
                                               WL_DELIVERY_UNRELIABLE);
    zassert_equal(send_result.domain, CONTROL_SEND_OK);
    zassert_equal(send_result.core_result, WL_OK);
    zassert_true(send_result.payload_length > 120U);
    expect_local_unreliable_completion(sender, sequence);
    deliver(sender, receiver);
    dispatch_latest(receiver, sequence);
  }

  zassert_ok(wl_latest_read_acquire(
      &latest_fixture.instance.arm_mit_command_latest, &view));
  zassert_equal(view.generation, 3U);
  zassert_equal(view.value_size, sizeof(arm_mit_command_t));
  const arm_mit_command_t *latest = view.value;
  zassert_true(latest->has_sequence);
  zassert_equal(latest->sequence, 3U);
  zassert_equal(latest->controls[0], 300.0F);
  zassert_equal(latest->controls[29], 329.0F);
  zassert_ok(wl_latest_read_release(
      &latest_fixture.instance.arm_mit_command_latest, &view));
  zassert_equal(wl_latest_read_acquire(
                    &latest_fixture.instance.arm_mit_command_latest, &view),
                WL_ERR_NO_DATA);

  zassert_ok(wl_latest_get_stats(
      &latest_fixture.instance.arm_mit_command_latest, &stats));
  zassert_equal(stats.publishes, 3U);
  zassert_equal(stats.reads, 1U);
  zassert_equal(stats.coalesced, 2U);
}

static void joint_fifo_init(void) {
  control_runtime_config_t config;

  memset(&joint_fifo, 0, sizeof(joint_fifo));
  config = retained_runtime_config(JOINT_FIFO_CAPACITY);
  (void)initialize_generated_runtime(&joint_fifo.instance, &joint_fifo.storage,
                                     &config);
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

  send_result = control_joint_command_send(&sender->ctx, &command,
                                           WL_DELIVERY_RELIABLE);
  zassert_equal(send_result.domain, CONTROL_SEND_OK);
  zassert_not_equal(send_result.handle, 0U);
  deliver(sender, receiver);

  event = poll_event(receiver, now_ms, WL_EVT_RELIABLE_RX);
  zassert_equal(event.message_id, JOINT_COMMAND_MESSAGE_ID);
  runtime_result = control_runtime_dispatch_event(
      &receiver->ctx, &event, &joint_fifo.instance.runtime, now_ms);
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
    zassert_equal(retained_detail(&result)->storage_result, WL_OK);
    zassert_equal(retained_detail(&result)->codec_status, WL_CODEC_OK);
  }

  result = send_joint_reliable(sender, receiver, 4U, 8U);
  zassert_equal(result.domain, CONTROL_RUNTIME_STORAGE_ERROR);
  zassert_equal(retained_detail(&result)->storage_result, WL_ERR_QUEUE_FULL);

  for (uint32_t sequence = 1U; sequence <= JOINT_FIFO_CAPACITY; ++sequence) {
    const joint_command_t *command;

    zassert_ok(
        wl_fifo_read_acquire(&joint_fifo.instance.joint_command_fifo, &view));
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
    zassert_ok(
        wl_fifo_read_release(&joint_fifo.instance.joint_command_fifo, &view));
  }
  zassert_equal(
      wl_fifo_read_acquire(&joint_fifo.instance.joint_command_fifo, &view),
      WL_ERR_NO_DATA);

  /* A frame after the full-queue path proves its RX lease was released once. */
  result = send_joint_reliable(sender, receiver, 5U, 10U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_ok(
      wl_fifo_read_acquire(&joint_fifo.instance.joint_command_fifo, &view));
  const joint_command_t *last = view.value;
  zassert_equal(last->position_bits, 5U);
  zassert_ok(
      wl_fifo_read_release(&joint_fifo.instance.joint_command_fifo, &view));

  zassert_ok(
      wl_fifo_get_stats(&joint_fifo.instance.joint_command_fifo, &stats));
  zassert_equal(stats.depth, 0U);
  zassert_equal(stats.high_watermark, JOINT_FIFO_CAPACITY);
  zassert_equal(stats.publishes, 4U);
  zassert_equal(stats.consumes, 4U);
  zassert_equal(stats.full_rejections, 1U);
  zassert_equal(stats.errors, 0U);
}

static int32_t handle_home_request(void *user_data,
                                   const home_request_t *message,
                                   const wl_rpc_server_request_t *server_request,
                                   wl_delivery_t delivery) {
  struct rpc_server_fixture *fixture = user_data;

  if (server_request == NULL || server_request->generation == 0U ||
      server_request->identity.operation_id != message->operation_id ||
      server_request->identity.peer_session_id == 0U ||
      !message->has_operation_id ||
      message->operation_id == 0U ||
      !message->has_joint_mask || delivery != WL_DELIVERY_RELIABLE) {
    return -1;
  }
  ++fixture->handler_calls;
  fixture->last_operation_id = message->operation_id;
  fixture->last_joint_mask = message->joint_mask;
  fixture->last_request = *server_request;
  fixture->last_delivery = delivery;
  return fixture->handler_result;
}

static void rpc_init(void) {
  control_runtime_config_t client_config;
  control_runtime_config_t server_config;

  memset(&rpc_client, 0, sizeof(rpc_client));
  memset(&rpc_server, 0, sizeof(rpc_server));
  client_config = retained_runtime_config(1U);
  client_config.rpc_client_enabled = 1U;
  client_config.rpc_client_slot_count = 1U;
  client_config.rpc_client_response_capacity = 16U;
  client_config.rpc_client_next_operation_id = 1U;
  server_config = retained_runtime_config(1U);
  server_config.rpc_server_enabled = 1U;
  server_config.rpc_server_pending_slot_count = 1U;
  server_config.rpc_server_cache_slot_count = 1U;
  server_config.rpc_server_response_capacity = 32U;
  server_config.rpc_server_cache_policy = WL_RPC_CACHE_REJECT_NEW;
  server_config.home_canonical_request_capacity = 32U;
  server_config.home_request_handler = handle_home_request;
  server_config.home_user_data = &rpc_server;

  (void)initialize_generated_runtime(&rpc_client.instance, &rpc_client.storage,
                                     &client_config);
  (void)initialize_generated_runtime(&rpc_server.instance, &rpc_server.storage,
                                     &server_config);
  zassert_equal(rpc_client.instance.runtime.rpc_client,
                &rpc_client.instance.rpc_client);
  zassert_equal(rpc_client.instance.runtime.home.response_scratch,
                &rpc_client.instance.home_scratch.response);
  zassert_equal(rpc_server.instance.runtime.rpc_server,
                &rpc_server.instance.rpc_server);
  zassert_equal(rpc_server.instance.runtime.home.request_scratch,
                &rpc_server.instance.home_scratch.request);
  zassert_not_null(
      rpc_server.instance.runtime.home.canonical_request_scratch.data);
  zassert_equal(
      rpc_server.instance.runtime.home.canonical_request_scratch.capacity,
      server_config.home_canonical_request_capacity);
  zassert_equal(rpc_server.instance.runtime.home.request_handler,
                handle_home_request);
  zassert_equal(rpc_server.instance.runtime.home.user_data, &rpc_server);
}

struct runtime_pump_state {
  control_runtime_t *runtime;
  wl_time_ms_t now_ms;
  control_runtime_result_t result;
};

static wl_pump_event_disposition_t
dispatch_with_pump(void *user_data, wl_ctx_t *ctx,
                   const wl_event_t *event) {
  struct runtime_pump_state *state = user_data;

  state->result = control_runtime_dispatch_event(ctx, event, state->runtime,
                                                  state->now_ms);
  return state->result.event_consumed != 0U ? WL_PUMP_EVENT_CONSUMED
                                            : WL_PUMP_EVENT_UNHANDLED;
}

static control_runtime_result_t
finish_reliable_tx(struct endpoint *endpoint, control_runtime_t *runtime,
                   wl_time_ms_t now_ms, wl_tx_handle_t expected_handle) {
  struct runtime_pump_state state = {
      .runtime = runtime,
      .now_ms = now_ms,
  };
  const wl_pump_hooks_t hooks = {
      .user_data = &state,
      .on_event = dispatch_with_pump,
  };
  wl_pump_result_t pump_result;
  wl_tx_state_t tx_state;

  zassert_ok(wl_pump_step(&endpoint->ctx, now_ms, 1U, &hooks, &pump_result));
  zassert_equal(pump_result.events, 1U);
  zassert_equal(state.result.event_type, WL_EVT_TX_SUCCESS);
  zassert_equal(rpc_detail(&state.result)->handle, expected_handle);
  zassert_equal(wl_tx_status(&endpoint->ctx, expected_handle, &tx_state),
                WL_ERR_NOT_FOUND);
  return state.result;
}

static control_runtime_result_t dispatch_home_request(struct endpoint *client,
                                                      struct endpoint *server,
                                                      wl_time_ms_t now_ms) {
  wl_event_t event;
  control_runtime_result_t result;

  deliver(client, server);
  event = poll_event(server, now_ms, WL_EVT_RELIABLE_RX);
  zassert_equal(event.message_id, HOME_REQUEST_MESSAGE_ID);
  /* Drain the link ACK so a REPLAY may submit its cached response immediately.
   */
  deliver(server, client);
  result = control_runtime_dispatch_event(&server->ctx, &event,
                                          &rpc_server.instance.runtime, now_ms);
  zassert_equal(result.message_id, HOME_REQUEST_MESSAGE_ID);
  zassert_equal(result.event_type, WL_EVT_RELIABLE_RX);
  return result;
}

static control_runtime_result_t send_request_copy(struct endpoint *client,
                                                  struct endpoint *server,
                                                  const home_request_t *request,
                                                  wl_time_ms_t now_ms) {
  const control_send_result_t sent = control_home_request_send(
      &client->ctx, request, WL_DELIVERY_RELIABLE);
  control_runtime_result_t result;
  control_runtime_result_t terminal;

  zassert_equal(sent.domain, CONTROL_SEND_OK);
  zassert_not_equal(sent.handle, 0U);
  result = dispatch_home_request(client, server, now_ms);
  terminal = finish_reliable_tx(client, &rpc_client.instance.runtime,
                                now_ms + 1U, sent.handle);
  zassert_equal(terminal.domain, CONTROL_RUNTIME_NON_RX);
  zassert_equal(rpc_detail(&terminal)->rpc_result, WL_RPC_ERR_NOT_FOUND);
  return result;
}

static control_runtime_result_t receive_home_response(struct endpoint *server,
                                                      struct endpoint *client,
                                                      const uint8_t *expected,
                                                      size_t expected_length,
                                                      wl_time_ms_t now_ms) {
  wl_event_t event;
  control_runtime_result_t result;

  deliver(server, client);
  event = poll_event(client, now_ms, WL_EVT_RELIABLE_RX);
  zassert_equal(event.message_id, HOME_RESPONSE_MESSAGE_ID);
  zassert_equal(event.payload_len, expected_length);
  zassert_mem_equal(event.payload, expected, expected_length);
  result = control_runtime_dispatch_event(&client->ctx, &event,
                                          &rpc_client.instance.runtime, now_ms);

  /* The generated dispatch must retain the response before releasing RX. */
  memset(client->rx_fallback, 0xA5, sizeof(client->rx_fallback));
  return result;
}

static void finish_response_tx(struct endpoint *client, struct endpoint *server,
                               wl_tx_handle_t handle, wl_time_ms_t now_ms) {
  control_runtime_result_t terminal;

  deliver(client, server);
  terminal =
      finish_reliable_tx(server, &rpc_server.instance.runtime, now_ms, handle);
  zassert_equal(terminal.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&terminal)->rpc_result, WL_RPC_OK);
}

static wl_tx_handle_t service_server_response(struct endpoint *server,
                                              wl_time_ms_t now_ms) {
  control_runtime_service_result_t service = {0};

  zassert_equal(control_runtime_service(&server->ctx,
                                        &rpc_server.instance.runtime, now_ms,
                                        &service),
                WL_RPC_OK);
  zassert_equal(service.responses_submitted, 1U);
  zassert_equal(service.responses_deferred, 0U);
  zassert_equal(service.response.domain, CONTROL_RUNTIME_OK);
  zassert_not_equal(rpc_detail(&service.response)->handle, 0U);
  return rpc_detail(&service.response)->handle;
}

ZTEST(wirelink_application_runtime,
      test_generated_rpc_correlates_and_deduplicates_complete) {
  struct endpoint *client = &endpoint_client;
  struct endpoint *server = &endpoint_server;
  home_request_t request;
  home_request_t conflict;
  home_response_t response;
  control_runtime_result_t result;
  control_runtime_result_t terminal;
  wl_rpc_client_result_t client_result;
  uint8_t cached_response[16];
  uint8_t expected_response[16];
  size_t expected_length = 0U;

  endpoint_init(client, UINT64_C(0x3333));
  endpoint_init(server, UINT64_C(0x4444));
  rpc_init();

  home_request_clear(&request);
  request.has_joint_mask = true;
  request.joint_mask = 0x3FU;
  result = control_home_client_start(
      &client->ctx, &rpc_client.instance.runtime, &request, 100U, 0U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->rpc_result, WL_RPC_OK);
  zassert_not_equal(rpc_detail(&result)->handle, 0U);
  zassert_not_equal(rpc_detail(&result)->operation_id, 0U);
  zassert_false(request.has_operation_id);
  zassert_equal(request.operation_id, 0U);
  zassert_equal(wl_rpc_client_get(&rpc_client.instance.rpc_client,
                                  rpc_detail(&result)->operation_id,
                                  &client_result),
                WL_RPC_OK);
  zassert_equal(client_result.state, WL_RPC_CLIENT_LINK_PENDING);
  zassert_equal(client_result.tx_handle, rpc_detail(&result)->handle);

  const wl_tx_handle_t request_handle = rpc_detail(&result)->handle;
  const uint32_t operation_id = rpc_detail(&result)->operation_id;
  request.has_operation_id = true;
  request.operation_id = operation_id;
  result = dispatch_home_request(client, server, 1U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->rpc_disposition, WL_RPC_SERVER_NEW);
  zassert_equal(rpc_detail(&result)->operation_id, operation_id);
  zassert_equal(rpc_server.handler_calls, 1U);
  zassert_equal(rpc_server.last_operation_id, operation_id);
  zassert_equal(rpc_server.last_joint_mask, request.joint_mask);
  zassert_equal(rpc_server.last_delivery, WL_DELIVERY_RELIABLE);

  /* Server application work is pending and the transport ACK is separate. */
  zassert_equal(wl_rpc_client_get(&rpc_client.instance.rpc_client, operation_id,
                                  &client_result),
                WL_RPC_OK);
  zassert_equal(client_result.state, WL_RPC_CLIENT_LINK_PENDING);
  terminal = finish_reliable_tx(client, &rpc_client.instance.runtime, 2U,
                                request_handle);
  zassert_equal(terminal.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&terminal)->rpc_result, WL_RPC_OK);
  zassert_equal(wl_rpc_client_get(&rpc_client.instance.rpc_client, operation_id,
                                  &client_result),
                WL_RPC_OK);
  zassert_equal(client_result.state, WL_RPC_CLIENT_WAIT_RESPONSE);
  zassert_equal(client_result.link_delivery_confirmed, 1U);

  result = send_request_copy(client, server, &request, 3U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->rpc_disposition,
                WL_RPC_SERVER_PENDING_DUPLICATE);
  zassert_equal(rpc_server.handler_calls, 1U);

  conflict = request;
  conflict.joint_mask ^= 1U;
  result = send_request_copy(client, server, &conflict, 5U);
  zassert_equal(result.domain, CONTROL_RUNTIME_RPC_ERROR);
  zassert_equal(rpc_detail(&result)->rpc_disposition, WL_RPC_SERVER_CONFLICT);
  zassert_equal(rpc_detail(&result)->rpc_result, WL_RPC_ERR_OPERATION_CONFLICT);
  zassert_equal(rpc_server.handler_calls, 1U);

  home_response_clear(&response);
  result = control_home_server_complete(&rpc_server.instance.runtime,
                                        &rpc_server.last_request, &response,
                                        7U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->rpc_result, WL_RPC_OK);
  zassert_false(response.has_operation_id);
  zassert_equal(response.operation_id, 0U);
  zassert_false(response.has_status);
  zassert_equal(response.status, OPERATION_OK);
  response.has_operation_id = true;
  response.operation_id = operation_id;
  response.has_status = true;
  zassert_equal(home_response_encode(&response, expected_response,
                                     sizeof(expected_response),
                                     &expected_length),
                WL_CODEC_OK);
  zassert_equal(rpc_detail(&result)->payload_length, expected_length);
  zassert_equal(rpc_detail(&result)->server_response.response_length,
                expected_length);
  zassert_mem_equal(rpc_detail(&result)->server_response.response_data,
                    expected_response, expected_length);
  memcpy(cached_response, rpc_detail(&result)->server_response.response_data,
         expected_length);

  const wl_tx_handle_t response_handle = service_server_response(server, 7U);
  result = receive_home_response(server, client, cached_response,
                                 expected_length, 8U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->rpc_result, WL_RPC_OK);
  zassert_equal(rpc_detail(&result)->operation_id, operation_id);
  zassert_equal(rpc_detail(&result)->application_result, OPERATION_OK);
  zassert_equal(wl_rpc_client_get(&rpc_client.instance.rpc_client, operation_id,
                                  &client_result),
                WL_RPC_OK);
  zassert_equal(client_result.state, WL_RPC_CLIENT_COMPLETED);
  zassert_equal(client_result.application_status, OPERATION_OK);
  zassert_equal(client_result.link_delivery_confirmed, 1U);
  zassert_equal(client_result.response_length, expected_length);
  zassert_mem_equal(client_result.response_data, cached_response,
                    expected_length);
  finish_response_tx(client, server, response_handle, 9U);

  result = send_request_copy(client, server, &request, 10U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->rpc_disposition, WL_RPC_SERVER_REPLAY);
  zassert_equal(rpc_detail(&result)->application_result, OPERATION_OK);
  zassert_equal(rpc_detail(&result)->server_response.response_length,
                expected_length);
  zassert_mem_equal(rpc_detail(&result)->server_response.response_data,
                    cached_response, expected_length);
  zassert_equal(rpc_server.handler_calls, 1U);

  const wl_tx_handle_t replay_handle = service_server_response(server, 11U);
  result = receive_home_response(server, client, cached_response,
                                 expected_length, 12U);
  zassert_equal(result.domain, CONTROL_RUNTIME_RPC_ERROR);
  zassert_equal(rpc_detail(&result)->rpc_result, WL_RPC_ERR_INVALID_STATE);
  finish_response_tx(client, server, replay_handle, 13U);
  zassert_equal(wl_rpc_client_get(&rpc_client.instance.rpc_client, operation_id,
                                  &client_result),
                WL_RPC_OK);
  zassert_mem_equal(client_result.response_data, cached_response,
                    expected_length);
  zassert_equal(
      wl_rpc_client_release(&rpc_client.instance.rpc_client, operation_id),
      WL_RPC_OK);

  /* Retry the same logical operation through the single generated start API.
   * The explicit ID addresses the completed server cache entry, so the
   * application handler is not executed again. */
  result = control_home_client_start(
      &client->ctx, &rpc_client.instance.runtime, &request, 100U, 14U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->operation_id, operation_id);
  zassert_true(request.has_operation_id);
  zassert_equal(request.operation_id, operation_id);
  const wl_tx_handle_t retry_request_handle = rpc_detail(&result)->handle;
  result = dispatch_home_request(client, server, 15U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->rpc_disposition, WL_RPC_SERVER_REPLAY);
  zassert_equal(rpc_server.handler_calls, 1U);
  terminal = finish_reliable_tx(client, &rpc_client.instance.runtime, 16U,
                                retry_request_handle);
  zassert_equal(terminal.domain, CONTROL_RUNTIME_OK);
  const wl_tx_handle_t retry_response_handle =
      service_server_response(server, 17U);
  result = receive_home_response(server, client, cached_response,
                                 expected_length, 18U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->operation_id, operation_id);
  finish_response_tx(client, server, retry_response_handle, 19U);
  zassert_equal(
      wl_rpc_client_release(&rpc_client.instance.rpc_client, operation_id),
      WL_RPC_OK);
}

ZTEST(wirelink_application_runtime,
      test_generated_rpc_reject_retains_and_replays_exact_bytes) {
  struct endpoint *client = &endpoint_client;
  struct endpoint *server = &endpoint_server;
  home_request_t request;
  home_response_t response;
  control_runtime_result_t result;
  control_runtime_result_t terminal;
  wl_rpc_client_result_t client_result;
  uint8_t cached_response[16];
  uint8_t expected_response[16];
  size_t expected_length = 0U;

  endpoint_init(client, UINT64_C(0x7777));
  endpoint_init(server, UINT64_C(0x8888));
  rpc_init();

  home_request_clear(&request);
  request.has_joint_mask = true;
  request.joint_mask = 0x01U;
  result = control_home_client_start(
      &client->ctx, &rpc_client.instance.runtime, &request, 100U, 20U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  const uint32_t operation_id = rpc_detail(&result)->operation_id;
  const wl_tx_handle_t request_handle = rpc_detail(&result)->handle;
  request.has_operation_id = true;
  request.operation_id = operation_id;

  result = dispatch_home_request(client, server, 21U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->rpc_disposition, WL_RPC_SERVER_NEW);
  zassert_equal(rpc_server.handler_calls, 1U);
  terminal = finish_reliable_tx(client, &rpc_client.instance.runtime, 22U,
                                request_handle);
  zassert_equal(terminal.domain, CONTROL_RUNTIME_OK);

  home_response_clear(&response);
  result = control_home_server_reject(
      &rpc_server.instance.runtime, &rpc_server.last_request,
      OPERATION_REJECTED, &response, 23U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->application_result, OPERATION_REJECTED);
  zassert_false(response.has_operation_id);
  zassert_equal(response.operation_id, 0U);
  zassert_false(response.has_status);
  zassert_equal(response.status, OPERATION_OK);
  response.has_operation_id = true;
  response.operation_id = operation_id;
  response.has_status = true;
  response.status = OPERATION_REJECTED;
  zassert_equal(home_response_encode(&response, expected_response,
                                     sizeof(expected_response),
                                     &expected_length),
                WL_CODEC_OK);
  zassert_equal(rpc_detail(&result)->server_response.response_length,
                expected_length);
  zassert_mem_equal(rpc_detail(&result)->server_response.response_data,
                    expected_response, expected_length);
  memcpy(cached_response, rpc_detail(&result)->server_response.response_data,
         expected_length);

  const wl_tx_handle_t response_handle = service_server_response(server, 23U);
  result = receive_home_response(server, client, cached_response,
                                 expected_length, 24U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->application_result, OPERATION_REJECTED);
  zassert_equal(wl_rpc_client_get(&rpc_client.instance.rpc_client, operation_id,
                                  &client_result),
                WL_RPC_OK);
  zassert_equal(client_result.state, WL_RPC_CLIENT_APPLICATION_ERROR);
  zassert_equal(client_result.application_status, OPERATION_REJECTED);
  zassert_equal(client_result.response_length, expected_length);
  zassert_mem_equal(client_result.response_data, cached_response,
                    expected_length);
  finish_response_tx(client, server, response_handle, 25U);

  result = send_request_copy(client, server, &request, 26U);
  zassert_equal(result.domain, CONTROL_RUNTIME_OK);
  zassert_equal(rpc_detail(&result)->rpc_disposition, WL_RPC_SERVER_REPLAY);
  zassert_equal(rpc_detail(&result)->application_result, OPERATION_REJECTED);
  zassert_equal(rpc_detail(&result)->server_response.response_length,
                expected_length);
  zassert_mem_equal(rpc_detail(&result)->server_response.response_data,
                    cached_response, expected_length);
  zassert_equal(rpc_server.handler_calls, 1U);

  const wl_tx_handle_t replay_handle = service_server_response(server, 27U);
  result = receive_home_response(server, client, cached_response,
                                 expected_length, 28U);
  zassert_equal(result.domain, CONTROL_RUNTIME_RPC_ERROR);
  zassert_equal(rpc_detail(&result)->rpc_result, WL_RPC_ERR_INVALID_STATE);
  finish_response_tx(client, server, replay_handle, 29U);
  zassert_equal(
      wl_rpc_client_release(&rpc_client.instance.rpc_client, operation_id),
      WL_RPC_OK);
}

ZTEST_SUITE(wirelink_application_runtime, NULL, NULL, NULL, NULL, NULL);
