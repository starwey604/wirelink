/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <zephyr/ztest.h>
#include "wirelink/wirelink.h"
#include "wirelink/frame.h"
#include "context.h"

extern size_t frame_encode_calls;
int __real_wl_frame_encode(const wl_wire_packet_t *, wl_envelope_type_t,
                          uint8_t *, size_t, size_t *);

typedef struct {
  wl_ctx_t ctx;
  wl_config_t config;
  uint8_t payload[128], unit[256], control[64], fifo[256], rx[256];
  wl_sink_result_t script[8];
  size_t script_length, calls;
  const uint8_t *bytes;
  size_t length;
  wl_io_token_t token;
} fixture_t;

static wl_sink_result_t sink(void *context, wl_io_token_t token,
                              const uint8_t *bytes, size_t length) {
  fixture_t *f = context;
  zassert_true(f->calls < f->script_length, "unexpected physical submission");
  f->token = token;
  f->bytes = bytes;
  f->length = length;
  return f->script[f->calls++];
}

static void init(fixture_t *f, wl_envelope_type_t envelope,
                  const wl_sink_result_t *script, size_t count) {
  memset(f, 0, sizeof(*f));
  f->config = (wl_config_t){.max_payload_len = sizeof(f->payload), .envelope = envelope,
      .integrity = WL_INTEGRITY_CRC32C, .session_id = 123U, .ack_timeout_ms = 5U,
      .max_retries = 2U};
  const wl_storage_t storage = {f->payload, sizeof(f->payload), f->unit, sizeof(f->unit),
      f->control, sizeof(f->control), f->fifo, sizeof(f->fifo), f->rx, sizeof(f->rx)};
  zassert_true(count <= ARRAY_SIZE(f->script));
  if (count != 0U) memcpy(f->script, script, count * sizeof(*script));
  f->script_length = count;
  zassert_ok(wl_init(&f->ctx, &f->config, &storage));
  zassert_ok(wl_set_sink(&f->ctx, sink, f));
  frame_encode_calls = 0U;
}

static void feed(fixture_t *f, const wl_wire_packet_t *packet) {
  uint8_t bytes[256];
  size_t length;
  zassert_ok(__real_wl_frame_encode(packet, f->config.envelope, bytes, sizeof(bytes), &length));
  zassert_ok(wl_feed_unit(&f->ctx, bytes, length));
}

static void ack(fixture_t *f) {
  const wl_wire_packet_t packet = {.type = WL_PACKET_ACK, .session_id = f->config.session_id,
      .sequence = 0U, .integrity = f->config.integrity};
  feed(f, &packet);
}

ZTEST(wirelink_protocol_unit, test_telemetry_preserves_reliable_payload_and_ack_timer) {
  for (int envelope = WL_ENVELOPE_COBS_STREAM; envelope <= WL_ENVELOPE_BUS_LENGTH16; ++envelope) {
    for (unsigned direct = 0U; direct < 4U; ++direct) {
      for (unsigned mode = 0U; mode < 3U; ++mode) {
        fixture_t f;
        const wl_sink_result_t script[] = {WL_SINK_SENT,
            mode == 0U ? WL_SINK_SENT : mode == 1U ? WL_SINK_STARTED : WL_SINK_BUSY,
            WL_SINK_SENT, WL_SINK_SENT};
        init(&f, envelope, script, ARRAY_SIZE(script));
        uint8_t original[256], message[128];
        for (size_t i = 0U; i < sizeof(message); ++i) message[i] = (uint8_t)i;
        wl_tx_handle_t handle;
        wl_tx_payload_claim_t claim;
        wl_event_t event;
        wl_tx_result_t result;
        if ((direct & 1U) != 0U) {
          zassert_ok(wl_tx_payload_claim(&f.ctx, 21U, WL_DELIVERY_RELIABLE, &claim));
          memcpy(claim.span.data, message, sizeof(message));
          zassert_ok(wl_tx_payload_commit(&f.ctx, &claim, sizeof(message), 100U, &handle));
        } else zassert_ok(wl_send_reliable(&f.ctx, 21U, message, sizeof(message), 100U, &handle));
        const size_t length = f.length;
        memcpy(original, f.bytes, length);
        memset(message, 0xBB, sizeof(message)); /* Accepted RPC already owns its request. */
        zassert_equal(wl_poll(&f.ctx, 101U, &event), WL_ERR_NO_DATA);
        if ((direct & 2U) != 0U) {
          zassert_ok(wl_tx_payload_claim(&f.ctx, 23U, WL_DELIVERY_UNRELIABLE, &claim));
          memcpy(claim.span.data, message, sizeof(message));
          zassert_ok(wl_tx_payload_commit(&f.ctx, &claim, sizeof(message), 0U, NULL));
        } else zassert_ok(wl_send_unreliable(&f.ctx, 23U, message, sizeof(message)));
        memset(message, 0xCC, sizeof(message));
        if (mode == 1U) zassert_ok(wl_tx_complete(&f.ctx, f.token, WL_OK));
        zassert_ok(wl_poll(&f.ctx, 102U, &event));
        zassert_equal(event.type, WL_EVT_TX_SUCCESS);
        zassert_equal(event.handle, 0U);
        zassert_equal(wl_ctx_impl(&f.ctx)->tx_state, WL_TX_STATE_WAITING_ACK);
        zassert_equal(wl_ctx_impl(&f.ctx)->tx_start_ts, 100U);
        zassert_equal(wl_ctx_impl(&f.ctx)->tx_handle, handle);
        zassert_equal(wl_poll(&f.ctx, 104U, &event), WL_ERR_NO_DATA);
        const size_t before = f.calls;
        zassert_equal(wl_poll(&f.ctx, 105U, &event), WL_ERR_NO_DATA);
        zassert_equal(f.calls, before + 1U);
        zassert_equal(f.length, length);
        zassert_mem_equal(f.bytes, original, length);
        zassert_equal(frame_encode_calls, 3U); /* Rebuild exactly once after telemetry. */
        zassert_ok(wl_tx_cancel(&f.ctx, handle));
        zassert_ok(wl_tx_take(&f.ctx, handle, &result));
        zassert_equal(result.retries_used, 1U);
      }
    }
  }
}

ZTEST(wirelink_protocol_unit, test_reliable_finish_does_not_retire_unreliable_io) {
  for (int envelope = WL_ENVELOPE_COBS_STREAM; envelope <= WL_ENVELOPE_BUS_LENGTH16; ++envelope) {
    for (unsigned cancel = 0U; cancel < 2U; ++cancel) {
      for (unsigned busy = 0U; busy < 2U; ++busy) {
        fixture_t f;
        const wl_sink_result_t script[] = {WL_SINK_SENT,
            busy != 0U ? WL_SINK_BUSY : WL_SINK_STARTED, WL_SINK_SENT};
        init(&f, envelope, script, ARRAY_SIZE(script));
        wl_tx_handle_t handle, blocked;
        wl_tx_result_t result;
        wl_event_t event;
        uint8_t bytes[256];
        zassert_ok(wl_send_reliable(&f.ctx, 21U, (const uint8_t *)"rpc", 3U, 0U, &handle));
        const wl_io_token_t previous = f.token;
        zassert_ok(wl_send_unreliable(&f.ctx, 23U, (const uint8_t *)"latest", 6U));
        memcpy(bytes, f.bytes, f.length);
        const size_t length = f.length;
        const wl_io_token_t telemetry_token = f.token;
        if (cancel != 0U) zassert_ok(wl_tx_cancel(&f.ctx, handle));
        else {
          ack(&f);
          zassert_ok(wl_poll(&f.ctx, 1U, &event));
          zassert_equal(event.type, WL_EVT_TX_SUCCESS);
          zassert_equal(event.handle, handle);
        }
        zassert_ok(wl_tx_take(&f.ctx, handle, &result));
        zassert_equal(result.state, cancel != 0U ? WL_TX_STATE_CANCELLED : WL_TX_STATE_SUCCESS);
        zassert_mem_equal(f.unit, bytes, length);
        zassert_equal(wl_send_reliable(&f.ctx, 21U, NULL, 0U, 1U, &blocked), WL_ERR_BUSY);
        zassert_equal(wl_tx_complete(&f.ctx, previous, WL_OK), WL_ERR_NOT_FOUND);
        if (busy == 0U) zassert_ok(wl_tx_complete(&f.ctx, telemetry_token, WL_OK));
        zassert_ok(wl_poll(&f.ctx, 2U, &event));
        zassert_equal(event.type, WL_EVT_TX_SUCCESS);
        zassert_equal(event.handle, 0U);
        zassert_equal(f.length, length);
        zassert_mem_equal(f.bytes, bytes, length);
        zassert_equal(wl_ctx_impl(&f.ctx)->tx_inflight, 0U);
        zassert_equal(wl_ctx_impl(&f.ctx)->tx_queued, 0U);
      }
    }
  }
}

ZTEST(wirelink_protocol_unit, test_claim_blocks_retry_and_expired_retry_blocks_new_telemetry) {
  for (int envelope = WL_ENVELOPE_COBS_STREAM; envelope <= WL_ENVELOPE_BUS_LENGTH16; ++envelope) {
    fixture_t f;
    const wl_sink_result_t script[] = {WL_SINK_SENT, WL_SINK_SENT};
    init(&f, envelope, script, ARRAY_SIZE(script));
    wl_tx_handle_t handle;
    wl_event_t event;
    wl_tx_payload_claim_t claim, blocked;
    wl_poll_hint_t hint;
    uint8_t original[256];
    zassert_ok(wl_send_reliable(&f.ctx, 21U, (const uint8_t *)"original", 8U, UINT32_MAX - 2U, &handle));
    memcpy(original, f.bytes, f.length);
    const size_t length = f.length;
    zassert_ok(wl_tx_payload_claim(&f.ctx, 23U, WL_DELIVERY_UNRELIABLE, &claim));
    memset(claim.span.data, 0x77, claim.span.length);
    zassert_equal(wl_poll(&f.ctx, 2U, &event), WL_ERR_NO_DATA);
    zassert_equal(f.calls, 1U);
    for (size_t i = 0U; i < claim.span.length; ++i) zassert_equal(claim.span.data[i], 0x77);
    zassert_ok(wl_poll_get_hint(&f.ctx, 2U, &hint));
    zassert_equal(hint.work_pending, 0U);
    zassert_equal(hint.next_deadline_ms, WL_POLL_NO_DEADLINE_MS);
    zassert_ok(wl_tx_payload_abort(&f.ctx, &claim));
    zassert_ok(wl_poll_get_hint(&f.ctx, 2U, &hint));
    zassert_equal(hint.work_pending, 1U);
    zassert_equal(hint.next_deadline_ms, 0U);
    zassert_equal(wl_send_unreliable(&f.ctx, 23U, NULL, 0U), WL_ERR_BUSY);
    zassert_equal(wl_tx_payload_claim(&f.ctx, 23U, WL_DELIVERY_UNRELIABLE, &blocked), WL_ERR_BUSY);
    zassert_equal(wl_poll(&f.ctx, 2U, &event), WL_ERR_NO_DATA);
    zassert_equal(f.calls, 2U);
    zassert_equal(f.length, length);
    zassert_mem_equal(f.bytes, original, length);
  }
}

ZTEST(wirelink_protocol_unit, test_late_ack_retires_retry_only_after_its_io_lease) {
  for (int envelope = WL_ENVELOPE_COBS_STREAM; envelope <= WL_ENVELOPE_BUS_LENGTH16; ++envelope) {
    for (unsigned failed_io = 0U; failed_io < 2U; ++failed_io) {
      fixture_t f;
      const wl_sink_result_t script[] = {WL_SINK_SENT, WL_SINK_STARTED};
      init(&f, envelope, script, ARRAY_SIZE(script));
      wl_tx_handle_t handle;
      wl_tx_result_t result;
      wl_event_t event;
      wl_tx_state_t state;
      wl_poll_hint_t hint;
      zassert_ok(wl_send_reliable(&f.ctx, 21U, NULL, 0U, 0U, &handle));
      zassert_equal(wl_poll(&f.ctx, 5U, &event), WL_ERR_NO_DATA);
      ack(&f);
      zassert_equal(wl_poll(&f.ctx, 6U, &event), WL_ERR_NO_DATA);
      zassert_ok(wl_tx_status(&f.ctx, handle, &state));
      zassert_equal(state, WL_TX_STATE_SUCCESS);
      zassert_equal(wl_tx_take(&f.ctx, handle, &result), WL_ERR_INVALID_STATE);
      zassert_ok(wl_poll_get_hint(&f.ctx, 100U, &hint));
      zassert_equal(hint.work_pending, 0U);
      zassert_ok(wl_tx_complete(&f.ctx, f.token, failed_io != 0U ? WL_ERR_IO : WL_OK));
      zassert_ok(wl_poll_get_hint(&f.ctx, 100U, &hint));
      zassert_equal(hint.work_pending, 1U);
      zassert_ok(wl_poll(&f.ctx, 100U, &event));
      zassert_equal(event.type, WL_EVT_TX_SUCCESS);
      zassert_equal(event.handle, handle);
      zassert_ok(wl_tx_take(&f.ctx, handle, &result));
      zassert_equal(result.result, WL_OK);
      zassert_equal(result.retries_used, 1U);
      zassert_ok(wl_tx_complete(&f.ctx, f.token, WL_ERR_IO));
      zassert_equal(wl_poll(&f.ctx, 101U, &event), WL_ERR_NO_DATA);
    }
  }
}

ZTEST(wirelink_protocol_unit, test_ack_cancels_busy_retry_without_another_submission) {
  fixture_t f;
  const wl_sink_result_t script[] = {WL_SINK_SENT, WL_SINK_BUSY};
  init(&f, WL_ENVELOPE_COBS_STREAM, script, ARRAY_SIZE(script));
  wl_tx_handle_t handle;
  wl_event_t event;
  wl_tx_result_t result;
  zassert_ok(wl_send_reliable(&f.ctx, 21U, NULL, 0U, 0U, &handle));
  zassert_equal(wl_poll(&f.ctx, 5U, &event), WL_ERR_NO_DATA);
  ack(&f);
  zassert_ok(wl_poll(&f.ctx, 6U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  zassert_ok(wl_tx_take(&f.ctx, handle, &result));
  zassert_equal(f.calls, 2U);
  zassert_equal(wl_ctx_impl(&f.ctx)->tx_queued, 0U);
}

ZTEST(wirelink_protocol_unit, test_io_retry_respects_pending_control_priority) {
  fixture_t f;
  const wl_sink_result_t script[] = {WL_SINK_STARTED, WL_SINK_STARTED, WL_SINK_SENT};
  init(&f, WL_ENVELOPE_COBS_STREAM, script, ARRAY_SIZE(script));
  wl_tx_handle_t handle;
  wl_event_t event;
  uint8_t original[256];
  zassert_ok(wl_send_reliable(&f.ctx, 21U, (const uint8_t *)"rpc", 3U, 0U, &handle));
  const wl_io_token_t data_token = f.token;
  const size_t length = f.length;
  memcpy(original, f.bytes, length);
  const wl_wire_packet_t peer = {.type = WL_PACKET_DATA, .flags = WL_PACKET_FLAG_RELIABLE,
      .message_id = 42U, .session_id = 456U, .sequence = 7U, .integrity = f.config.integrity};
  feed(&f, &peer);
  zassert_ok(wl_poll(&f.ctx, 1U, &event));
  wl_event_release(&f.ctx, &event);
  zassert_equal(f.calls, 1U);
  zassert_ok(wl_tx_complete(&f.ctx, data_token, WL_ERR_IO));
  zassert_equal(f.calls, 2U);
  zassert_equal_ptr(f.bytes, f.control);
  zassert_equal(wl_poll(&f.ctx, 10U, &event), WL_ERR_NO_DATA);
  zassert_equal(f.calls, 2U);
  zassert_ok(wl_tx_complete(&f.ctx, f.token, WL_OK));
  zassert_equal(wl_poll(&f.ctx, 10U, &event), WL_ERR_NO_DATA);
  zassert_equal(f.calls, 3U);
  zassert_equal(f.length, length);
  zassert_mem_equal(f.bytes, original, length);
  zassert_equal(wl_ctx_impl(&f.ctx)->tx_retries_used, 1U);
}

ZTEST(wirelink_protocol_unit, test_unreliable_io_error_does_not_fail_reliable_transaction) {
  for (unsigned queued = 0U; queued < 2U; ++queued) {
    fixture_t f;
    const wl_sink_result_t script[] = {WL_SINK_SENT,
        queued != 0U ? WL_SINK_BUSY : WL_SINK_STARTED, WL_SINK_FAILED, WL_SINK_SENT};
    init(&f, WL_ENVELOPE_NATIVE_PACKET, script, ARRAY_SIZE(script));
    wl_tx_handle_t handle;
    wl_event_t event;
    wl_tx_result_t result;
    zassert_ok(wl_send_reliable(&f.ctx, 21U, NULL, 0U, 0U, &handle));
    zassert_ok(wl_send_unreliable(&f.ctx, 23U, NULL, 0U));
    if (queued == 0U) zassert_ok(wl_tx_complete(&f.ctx, f.token, WL_ERR_IO));
    zassert_equal(wl_poll(&f.ctx, 1U, &event), WL_ERR_NO_DATA);
    zassert_equal(wl_ctx_impl(&f.ctx)->tx_state, WL_TX_STATE_WAITING_ACK);
    zassert_equal(wl_ctx_impl(&f.ctx)->tx_queued, 0U);
    zassert_equal(wl_ctx_impl(&f.ctx)->tx_inflight, 0U);
    ack(&f);
    zassert_ok(wl_poll(&f.ctx, 2U, &event));
    zassert_equal(event.type, WL_EVT_TX_SUCCESS);
    zassert_ok(wl_tx_take(&f.ctx, handle, &result));
    zassert_equal(result.result, WL_OK);
  }
}

ZTEST(wirelink_protocol_unit, test_ack_before_first_local_completion_is_not_a_retry_ack) {
  fixture_t f;
  const wl_sink_result_t script[] = {WL_SINK_STARTED};
  init(&f, WL_ENVELOPE_NATIVE_PACKET, script, ARRAY_SIZE(script));
  wl_tx_handle_t handle;
  wl_event_t event;
  wl_tx_result_t result;
  zassert_ok(wl_send_reliable(&f.ctx, 21U, NULL, 0U, 0U, &handle));
  ack(&f);
  zassert_equal(wl_poll(&f.ctx, 3U, &event), WL_ERR_NO_DATA);
  zassert_equal(wl_ctx_impl(&f.ctx)->tx_state, WL_TX_STATE_SENDING);
  zassert_ok(wl_tx_complete(&f.ctx, f.token, WL_OK));
  zassert_equal(wl_poll(&f.ctx, 3U, &event), WL_ERR_NO_DATA);
  ack(&f);
  zassert_ok(wl_poll(&f.ctx, 3U, &event));
  zassert_ok(wl_tx_take(&f.ctx, handle, &result));
  zassert_equal(result.result, WL_OK);
}

ZTEST(wirelink_protocol_unit, test_reliable_io_failure_event_survives_pending_rx) {
  fixture_t f;
  const wl_sink_result_t script[] = {WL_SINK_STARTED, WL_SINK_STARTED, WL_SINK_STARTED};
  init(&f, WL_ENVELOPE_NATIVE_PACKET, script, ARRAY_SIZE(script));
  wl_tx_handle_t handle;
  wl_event_t event;
  wl_tx_result_t result;
  zassert_ok(wl_send_reliable(&f.ctx, 21U, NULL, 0U, 0U, &handle));
  zassert_ok(wl_tx_complete(&f.ctx, f.token, WL_ERR_IO));
  zassert_ok(wl_tx_complete(&f.ctx, f.token, WL_ERR_IO));
  const wl_wire_packet_t incoming = {.type = WL_PACKET_DATA, .message_id = 23U,
      .integrity = f.config.integrity};
  feed(&f, &incoming);
  zassert_ok(wl_tx_complete(&f.ctx, f.token, WL_ERR_IO));
  zassert_ok(wl_poll(&f.ctx, 1U, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  wl_event_release(&f.ctx, &event);
  zassert_ok(wl_poll(&f.ctx, 1U, &event));
  zassert_equal(event.type, WL_EVT_TX_FAILED);
  zassert_equal(event.handle, handle);
  zassert_ok(wl_tx_take(&f.ctx, handle, &result));
  zassert_equal(result.result, WL_ERR_IO);
  zassert_equal(wl_poll(&f.ctx, 1U, &event), WL_ERR_NO_DATA);
}

ZTEST(wirelink_protocol_unit, test_tx_storage_lifetimes_require_disjoint_regions) {
  fixture_t f;
  init(&f, WL_ENVELOPE_NATIVE_PACKET, NULL, 0U);
  const wl_storage_t original = wl_ctx_impl(&f.ctx)->storage;
  wl_ctx_t ctx;
  wl_storage_t storage = original;
  storage.tx_payload = storage.tx_unit;
  zassert_equal(wl_init(&ctx, &f.config, &storage), WL_ERR_INVALID_ARG);
  storage = original;
  storage.control_unit = storage.tx_payload + 1U;
  zassert_equal(wl_init(&ctx, &f.config, &storage), WL_ERR_INVALID_ARG);
  storage = original;
  storage.control_unit = storage.tx_unit + storage.tx_unit_size - 1U;
  zassert_equal(wl_init(&ctx, &f.config, &storage), WL_ERR_INVALID_ARG);
  zassert_ok(wl_init(&ctx, &f.config, &original));
}
