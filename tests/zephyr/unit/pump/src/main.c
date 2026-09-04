/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/frame.h"
#include "wirelink/pump.h"
#include "wirelink/port.h"

struct fixture {
  wl_ctx_t ctx;
  wl_config_t config;
  uint8_t tx_payload[64];
  uint8_t tx_unit[256];
  uint8_t control_unit[64];
  uint8_t rx_fifo[256];
  uint8_t rx_fallback[256];
};

struct hook_state {
  unsigned int order;
  unsigned int service_order;
  unsigned int event_order;
  unsigned int application_order;
  unsigned int events;
  unsigned int quiesced;
  wl_tx_handle_t terminal_handle;
  int terminal_take_result;
};

static struct fixture test_fixture;

static wl_sink_result_t sent_sink(void *user_data, wl_io_token_t token,
                                  const uint8_t *data, size_t length) {
  (void)user_data;
  (void)token;
  (void)data;
  (void)length;
  return WL_SINK_SENT;
}

static struct fixture *fixture_init(uint32_t ack_timeout_ms) {
  struct fixture *fixture = &test_fixture;
  wl_storage_t storage;

  memset(fixture, 0, sizeof(*fixture));
  fixture->config = (wl_config_t){
      .max_payload_len = sizeof(fixture->tx_payload),
      .envelope = WL_ENVELOPE_NATIVE_PACKET,
      .integrity = WL_INTEGRITY_NONE,
      .session_id = UINT64_C(0x50554d505f544553),
      .max_retries = 0U,
      .ack_timeout_ms = ack_timeout_ms,
      .max_transmission_unit = sizeof(fixture->tx_unit),
  };
  storage = (wl_storage_t){
      .tx_payload = fixture->tx_payload,
      .tx_payload_size = sizeof(fixture->tx_payload),
      .tx_unit = fixture->tx_unit,
      .tx_unit_size = sizeof(fixture->tx_unit),
      .control_unit = fixture->control_unit,
      .control_unit_size = sizeof(fixture->control_unit),
      .rx_fifo = fixture->rx_fifo,
      .rx_fifo_size = sizeof(fixture->rx_fifo),
      .rx_fallback = fixture->rx_fallback,
      .rx_fallback_size = sizeof(fixture->rx_fallback),
  };
  zassert_ok(wl_init(&fixture->ctx, &fixture->config, &storage));
  zassert_ok(wl_set_sink(&fixture->ctx, sent_sink, NULL));
  return fixture;
}

static size_t encode_unit(const struct fixture *fixture, uint16_t message_id,
                          uint8_t *output, size_t output_size) {
  const uint8_t payload[] = {1U, 2U, 3U};
  const wl_wire_packet_t packet = {
      .type = WL_PACKET_DATA,
      .integrity = fixture->config.integrity,
      .flags = 0U,
      .message_id = message_id,
      .session_id = UINT64_C(0x504545525f544553),
      .sequence = 0U,
      .payload = payload,
      .payload_len = sizeof(payload),
  };
  size_t length = 0U;

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, output,
                             output_size, &length));
  return length;
}

static int ordered_service(void *user_data) {
  struct hook_state *state = user_data;

  state->service_order = ++state->order;
  return WL_OK;
}

static wl_pump_event_disposition_t ordered_event(void *user_data,
                                                  wl_ctx_t *ctx,
                                                  const wl_event_t *event) {
  struct hook_state *state = user_data;

  state->event_order = ++state->order;
  state->events++;
  if (event->type == WL_EVT_UNRELIABLE_RX ||
      event->type == WL_EVT_RELIABLE_RX) {
    wl_event_release(ctx, event);
    return WL_PUMP_EVENT_CONSUMED;
  } else {
    wl_tx_state_t tx_state = WL_TX_STATE_IDLE;

    state->terminal_handle = event->handle;
    zassert_ok(wl_tx_status(ctx, event->handle, &tx_state));
    zassert_equal(tx_state, WL_TX_STATE_FAILED);
  }
  return WL_PUMP_EVENT_UNHANDLED;
}

static wl_pump_event_disposition_t consuming_event(
    void *user_data, wl_ctx_t *ctx, const wl_event_t *event) {
  struct hook_state *state = user_data;
  wl_tx_result_t ignored;

  state->terminal_handle = event->handle;
  state->terminal_take_result = wl_tx_take(ctx, event->handle, &ignored);
  return WL_PUMP_EVENT_CONSUMED;
}

static uint8_t ordered_application(void *user_data, wl_ctx_t *ctx,
                                   wl_time_ms_t now_ms) {
  struct hook_state *state = user_data;

  (void)ctx;
  (void)now_ms;
  state->application_order = ++state->order;
  return 1U;
}

static uint32_t application_deadline(const void *user_data,
                                     wl_time_ms_t now_ms) {
  (void)user_data;
  (void)now_ms;
  return 7U;
}

static uint32_t adapter_deadline(const void *user_data,
                                 wl_time_ms_t now_ms) {
  (void)user_data;
  (void)now_ms;
  return 3U;
}

static void mark_quiesced(void *user_data) {
  struct hook_state *state = user_data;

  state->quiesced++;
}

ZTEST(wirelink_pump_unit, test_owner_pass_order_and_default_release) {
  struct fixture *fixture = fixture_init(5U);
  struct hook_state state = {0};
  wl_pump_hooks_t hooks = {
      .user_data = &state,
      .service = ordered_service,
      .application_progress = ordered_application,
      .on_event = ordered_event,
  };
  wl_pump_result_t result;
  uint8_t unit[128];
  size_t length = encode_unit(fixture, 0x41U, unit, sizeof(unit));

  zassert_ok(wl_feed_unit(&fixture->ctx, unit, length));
  zassert_ok(wl_pump_step(&fixture->ctx, 10U, 4U, &hooks, &result));
  zassert_equal(result.events, 1U);
  zassert_equal(result.rx_events, 1U);
  zassert_equal(result.progress, 1U);
  zassert_true(state.service_order < state.event_order);
  zassert_true(state.event_order < state.application_order);

  length = encode_unit(fixture, 0x42U, unit, sizeof(unit));
  zassert_ok(wl_feed_unit(&fixture->ctx, unit, length));
  zassert_ok(wl_pump_step(&fixture->ctx, 11U, 4U, NULL, &result));
  zassert_equal(result.rx_events, 1U);
  length = encode_unit(fixture, 0x43U, unit, sizeof(unit));
  zassert_ok(wl_feed_unit(&fixture->ctx, unit, length));
}

ZTEST(wirelink_pump_unit, test_deadline_merge_and_quiesce) {
  struct fixture *fixture = fixture_init(5U);
  struct hook_state state = {0};
  const wl_pump_hooks_t hooks = {
      .user_data = &state,
      .quiesce = mark_quiesced,
      .application_deadline_hint = application_deadline,
      .adapter_deadline_hint = adapter_deadline,
  };
  wl_poll_hint_t hint;

  zassert_ok(wl_pump_get_hint(&fixture->ctx, 0U, &hooks, &hint));
  zassert_equal(hint.work_pending, 0U);
  zassert_equal(hint.next_deadline_ms, 3U);
  wl_pump_quiesce(&hooks);
  zassert_equal(state.quiesced, 1U);
}

ZTEST(wirelink_pump_unit, test_terminal_callback_precedes_handle_take) {
  struct fixture *fixture = fixture_init(5U);
  struct hook_state state = {0};
  const wl_pump_hooks_t hooks = {
      .user_data = &state,
      .on_event = ordered_event,
  };
  wl_pump_result_t result;
  wl_tx_handle_t handle = 0U;
  wl_event_t ignored;
  wl_tx_state_t tx_state;

  zassert_equal(wl_poll(&fixture->ctx, 100U, &ignored), WL_ERR_NO_DATA);
  zassert_ok(wl_send_reliable(&fixture->ctx, 7U, NULL, 0U, &handle));
  zassert_ok(wl_pump_step(&fixture->ctx, 105U, 4U, &hooks, &result));
  zassert_equal(result.events, 1U);
  zassert_equal(state.terminal_handle, handle);
  zassert_equal(wl_tx_status(&fixture->ctx, handle, &tx_state),
                WL_ERR_NOT_FOUND);
}

ZTEST(wirelink_pump_unit, test_consumed_terminal_is_not_owned_by_pump) {
  struct fixture *fixture = fixture_init(5U);
  struct hook_state state = {0};
  const wl_pump_hooks_t hooks = {
      .user_data = &state,
      .on_event = consuming_event,
  };
  wl_pump_result_t result;
  wl_tx_handle_t handle = 0U;
  wl_event_t ignored;
  wl_tx_state_t tx_state;

  zassert_equal(wl_poll(&fixture->ctx, 100U, &ignored), WL_ERR_NO_DATA);
  zassert_ok(wl_send_reliable(&fixture->ctx, 7U, NULL, 0U, &handle));
  zassert_ok(wl_pump_step(&fixture->ctx, 105U, 4U, &hooks, &result));
  zassert_equal(state.terminal_handle, handle);
  zassert_equal(state.terminal_take_result, WL_OK);
  zassert_equal(wl_tx_status(&fixture->ctx, handle, &tx_state),
                WL_ERR_NOT_FOUND);
}

ZTEST(wirelink_pump_unit, test_validation) {
  struct fixture *fixture = fixture_init(5U);
  wl_pump_result_t result;
  wl_poll_hint_t hint;

  zassert_equal(wl_pump_step(NULL, 0U, 1U, NULL, &result),
                WL_ERR_INVALID_ARG);
  zassert_equal(wl_pump_step(&fixture->ctx, 0U, 0U, NULL, &result),
                WL_ERR_INVALID_ARG);
  zassert_equal(wl_pump_step(&fixture->ctx, 0U, 1U, NULL, NULL),
                WL_ERR_INVALID_ARG);
  zassert_equal(wl_pump_get_hint(NULL, 0U, NULL, &hint),
                WL_ERR_INVALID_ARG);
  zassert_equal(wl_pump_get_hint(&fixture->ctx, 0U, NULL, NULL),
                WL_ERR_INVALID_ARG);
}

ZTEST_SUITE(wirelink_pump_unit, NULL, NULL, NULL, NULL, NULL);
