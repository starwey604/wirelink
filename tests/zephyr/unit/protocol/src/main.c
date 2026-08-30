/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/wirelink.h"
#include "context.h"

struct test_sink_capture {
  wl_io_token_t last_token;
  size_t call_count;
  wl_sink_result_t scripted[8];
  size_t script_len;
  size_t script_idx;
};

static wl_sink_result_t test_sink_fn(void *user_data, wl_io_token_t token, const uint8_t *data,
                        size_t len) {
  struct test_sink_capture *cap = (struct test_sink_capture *)user_data;
  (void)data;
  (void)len;

  cap->last_token = token;
  cap->call_count++;

  if (cap->script_idx < cap->script_len) {
    return cap->scripted[cap->script_idx++];
  }

  return WL_SINK_FAILED;
}

static void init_ctx_and_sink(struct test_sink_capture *cap, wl_ctx_t *ctx,
                             const wl_config_t *cfg, wl_io_token_t default_token,
                             uint8_t *rx_mem, size_t rx_len,
                             uint8_t *tx_mem, size_t tx_len,
                             wl_sink_result_t *script, size_t script_len) {
  static uint8_t control_mem[WL_FRAME_MAX_COBS_LEN];
  static uint8_t fifo_mem[WL_FRAME_MAX_COBS_LEN];
  static uint8_t payload_mem[WL_FRAME_MAX_PAYLOAD];
  wl_storage_t storage = {
    .tx_payload = payload_mem,
    .tx_payload_size = sizeof(payload_mem),
    .tx_unit = tx_mem,
    .tx_unit_size = tx_len,
    .control_unit = control_mem,
    .control_unit_size = sizeof(control_mem),
    .rx_fifo = fifo_mem,
    .rx_fifo_size = sizeof(fifo_mem),
    .rx_fallback = rx_mem,
    .rx_fallback_size = rx_len,
  };
  (void)default_token;

  memset(cap, 0, sizeof(*cap));
  memset(ctx, 0, sizeof(*ctx));
  memset((void *)rx_mem, 0xAA, rx_len);
  memset((void *)tx_mem, 0x55, tx_len);

  cap->scripted[0] = WL_SINK_STARTED;
  cap->scripted[1] = WL_SINK_STARTED;
  cap->scripted[2] = WL_SINK_FAILED;
  cap->script_len = 3U;

  for (size_t i = 0; i < script_len && i < 8; ++i) {
    cap->scripted[i] = script[i];
  }
  cap->script_len = (script_len > 8U) ? 8U : script_len;

  zassert_ok(wl_init(ctx, cfg, &storage));
  zassert_ok(wl_set_sink(ctx, test_sink_fn, cap));
}

ZTEST(wirelink_protocol_unit, test_init_copies_configuration)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};
  wl_config_t cfg = {
    .max_payload_len = 128U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC32C,
    .session_id = UINT64_C(0x12345678AABBCCDD),
    .max_retries = 2U,
    .ack_timeout_ms = 5U,
    .max_transmission_unit = sizeof(tx_mem),
  };
  wl_config_t copied = {0};
  wl_sink_result_t script[] = {WL_SINK_SENT};

  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                    sizeof(tx_mem), script, 1U);
  memset(&cfg, 0, sizeof(cfg));

  zassert_ok(wl_get_config(&ctx, &copied));
  zassert_equal(copied.max_payload_len, 128U);
  zassert_equal(copied.envelope, WL_ENVELOPE_NATIVE_PACKET);
  zassert_equal(copied.integrity, WL_INTEGRITY_CRC32C);
  zassert_equal(copied.session_id, UINT64_C(0x12345678AABBCCDD));
  zassert_equal(copied.max_retries, 2U);
  zassert_equal(copied.ack_timeout_ms, 5U);
  zassert_equal(copied.max_transmission_unit, sizeof(tx_mem));
  zassert_ok(wl_send_unreliable(&ctx, 1U, NULL, 0U));
}

ZTEST(wirelink_protocol_unit, test_busy_on_send_is_would_block)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};

  wl_config_t cfg = {
    .max_payload_len = 128U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC32C,
    .session_id = 0x12345678AABBCCDDULL,
    .max_retries = 0,
    .ack_timeout_ms = 5U,
  };

  wl_sink_result_t script[] = {WL_SINK_BUSY};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 1);

  zassert_ok(wl_send_unreliable(&ctx, 1U, (const uint8_t *)"", 0U));
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_SENDING);
  zassert_equal(wl_ctx_impl(&ctx)->tx_queued, 1U);
}

ZTEST(wirelink_protocol_unit, test_busy_send_clears_queue_when_retry_starts)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};
  wl_event_t event = {0};

  wl_config_t cfg = {
    .max_payload_len = 128U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC32C,
    .session_id = 0x12345678AABBCCDDULL,
    .max_retries = 0,
    .ack_timeout_ms = 5U,
  };

  wl_sink_result_t script[] = {WL_SINK_BUSY, WL_SINK_STARTED};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 2);

  zassert_ok(wl_send_unreliable(&ctx, 1U, (const uint8_t *)"", 0U));
  zassert_equal(wl_ctx_impl(&ctx)->tx_queued, 1U);
  zassert_equal(wl_poll(&ctx, 1U, &event), WL_ERR_NO_DATA);
  zassert_equal(wl_ctx_impl(&ctx)->tx_queued, 0U);
  zassert_equal(wl_ctx_impl(&ctx)->tx_inflight, 1U);
}

ZTEST(wirelink_protocol_unit, test_reliable_send_busy_does_not_occupy_tx_slot)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};

  wl_config_t cfg = {
    .max_payload_len = 128U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC32C,
    .session_id = 0x2233445566778899ULL,
    .max_retries = 1U,
    .ack_timeout_ms = 20U,
  };

  wl_sink_result_t script[] = {WL_SINK_BUSY};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 1);

  wl_tx_handle_t handle = 0U;
  zassert_ok(wl_send_reliable(&ctx, 3U, (const uint8_t *)"B", 1U, &handle));
  zassert_not_equal(handle, 0U);
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_SENDING);
  zassert_equal(wl_ctx_impl(&ctx)->tx_queued, 1U);
}

ZTEST(wirelink_protocol_unit, test_send_reliable_blocked_while_waiting_ack)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};
  wl_tx_handle_t handle = 0;

  wl_config_t cfg = {
    .max_payload_len = 128U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC16,
    .session_id = 0xCAFEC0DEC0DEC0DEULL,
    .max_retries = 0U,
    .ack_timeout_ms = 20U,
  };

  wl_sink_result_t script[] = {WL_SINK_STARTED};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 1);

  zassert_ok(wl_send_reliable(&ctx, 1U, (const uint8_t *)"Q", 1U, &handle));
  zassert_ok(wl_tx_complete(&ctx, cap.last_token, WL_OK));
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_WAITING_ACK);
  zassert_equal(wl_send_reliable(&ctx, 2U, (const uint8_t *)"R", 1U, &handle),
                WL_ERR_BUSY);
}

ZTEST(wirelink_protocol_unit, test_send_zero_session_is_invalid)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};
  wl_config_t cfg = {
    .max_payload_len = 128U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC16,
    .session_id = 0x1122334455667788ULL,
    .max_retries = 0U,
    .ack_timeout_ms = 20U,
  };

  wl_sink_result_t script[] = {WL_SINK_SENT};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 1);

  wl_ctx_impl(&ctx)->session_id = 0U;
  zassert_equal(wl_send_unreliable(&ctx, 7U, (const uint8_t *)"A", 1U),
                WL_ERR_INVALID_ARG);
}

ZTEST(wirelink_protocol_unit, test_send_rejects_payload_over_max_config)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};

  wl_config_t cfg = {
    .max_payload_len = 2U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_NONE,
    .session_id = 0x2233445566778899ULL,
    .max_retries = 0U,
    .ack_timeout_ms = 20U,
  };

  wl_sink_result_t script[] = {WL_SINK_SENT};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 1);

  const uint8_t payload[] = {1, 2, 3};
  zassert_equal(wl_send_unreliable(&ctx, 8U, payload, sizeof(payload)),
                WL_ERR_PAYLOAD_TOO_LONG);
}

ZTEST(wirelink_protocol_unit, test_reliable_send_started_and_complete)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};
  wl_tx_handle_t handle = 0;

  wl_config_t cfg = {
    .max_payload_len = 128U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC16,
    .session_id = 0x0102030405060708ULL,
    .max_retries = 1U,
    .ack_timeout_ms = 20U,
  };

  wl_sink_result_t script[] = {WL_SINK_STARTED};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 1);

  zassert_ok(wl_send_reliable(&ctx, 2U, (const uint8_t *)"A", 1U, &handle));
  zassert_not_equal(handle, 0U);
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_SENDING);
  zassert_equal(wl_ctx_impl(&ctx)->tx_token, 2U);

  zassert_ok(wl_tx_complete(&ctx, cap.last_token, WL_OK));
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_WAITING_ACK);
}

ZTEST(wirelink_protocol_unit, test_ack_match_drives_tx_success)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};
  wl_tx_handle_t handle = 0;
  wl_event_t event = {0};

  wl_config_t cfg = {
    .max_payload_len = 64U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC32C,
    .session_id = 0xA0A1A2A3A4A5A6A7ULL,
    .max_retries = 1U,
    .ack_timeout_ms = 20U,
  };

  wl_sink_result_t script[] = {WL_SINK_SENT};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 1);

  zassert_ok(wl_send_reliable(&ctx, 9U, NULL, 0U, &handle));
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_WAITING_ACK);
  zassert_not_equal(handle, 0U);

  wl_wire_packet_t ack_packet = {
    .type = WL_PACKET_ACK,
    .integrity = WL_INTEGRITY_CRC32C,
    .flags = 0U,
    .message_id = 0U,
    .session_id = wl_ctx_impl(&ctx)->session_id,
    .sequence = 0U,
    .payload = NULL,
    .payload_len = 0U,
  };
  ack_packet.sequence = wl_ctx_impl(&ctx)->tx_waiting_seq;

  uint8_t ack_wire[WL_FRAME_MAX_RAW_LEN];
  size_t ack_len = 0U;
  zassert_ok(wl_frame_encode(&ack_packet, WL_ENVELOPE_NATIVE_PACKET, ack_wire,
                            sizeof(ack_wire), &ack_len));

  zassert_ok(wl_feed_unit(&ctx, ack_wire, ack_len));
  zassert_ok(wl_poll(&ctx, 0U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  zassert_equal(event.handle, handle);
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_SUCCESS);

  zassert_ok(wl_tx_complete(&ctx, cap.last_token, WL_OK));
  wl_tx_result_t result = {0};
  zassert_ok(wl_tx_take(&ctx, handle, &result));
  zassert_equal(result.state, WL_TX_STATE_SUCCESS);
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_IDLE);

  zassert_equal(wl_poll(&ctx, 1U, &event), WL_ERR_NO_DATA);
}

ZTEST(wirelink_protocol_unit, test_reliable_send_ack_closes_loop_from_waiting_ack)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};
  wl_tx_handle_t handle = 0;
  wl_event_t event = {0};
  wl_tx_state_t tx_state = WL_TX_STATE_IDLE;

  wl_config_t cfg = {
    .max_payload_len = 128U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC16,
    .session_id = 0x13579BDF2468ACE0ULL,
    .max_retries = 1U,
    .ack_timeout_ms = 15U,
  };

  wl_sink_result_t script[] = {WL_SINK_STARTED};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 1);

  zassert_ok(wl_send_reliable(&ctx, 4U, (const uint8_t *)"HELLO", 5U, &handle));
  zassert_ok(wl_tx_complete(&ctx, cap.last_token, WL_OK));
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_WAITING_ACK);

  wl_wire_packet_t ack_packet = {
    .type = WL_PACKET_ACK,
    .integrity = WL_INTEGRITY_CRC16,
    .flags = 0U,
    .message_id = 0U,
    .session_id = wl_ctx_impl(&ctx)->session_id,
    .sequence = 0U,
    .payload = NULL,
    .payload_len = 0U,
  };
  ack_packet.sequence = wl_ctx_impl(&ctx)->tx_waiting_seq;
  uint8_t ack_wire[WL_FRAME_MAX_RAW_LEN];
  size_t ack_len = 0U;
  zassert_ok(wl_frame_encode(&ack_packet, WL_ENVELOPE_NATIVE_PACKET, ack_wire,
                            sizeof(ack_wire), &ack_len));

  zassert_ok(wl_feed_unit(&ctx, ack_wire, ack_len));
  zassert_ok(wl_poll(&ctx, 7U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  zassert_equal(event.handle, handle);
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_SUCCESS);
  zassert_ok(wl_tx_status(&ctx, handle, &tx_state));
  zassert_equal(tx_state, WL_TX_STATE_SUCCESS);

  zassert_ok(wl_feed_unit(&ctx, ack_wire, ack_len));
  event = (wl_event_t){0};
  zassert_equal(wl_poll(&ctx, 8U, &event), WL_ERR_NO_DATA);
}

ZTEST(wirelink_protocol_unit, test_retries_timeout_path)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};
  wl_tx_handle_t handle = 0;
  wl_event_t event = {0};

  wl_config_t cfg = {
    .max_payload_len = 32U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC16,
    .session_id = 0xFEEDFACE11223344ULL,
    .max_retries = 2U,
    .ack_timeout_ms = 5U,
  };

  wl_sink_result_t script[] = {WL_SINK_STARTED, WL_SINK_STARTED, WL_SINK_STARTED};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 3);

  zassert_ok(wl_send_reliable(&ctx, 1U, NULL, 0U, &handle));
  zassert_ok(wl_tx_complete(&ctx, cap.last_token, WL_OK));
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_WAITING_ACK);

  zassert_equal(wl_poll(&ctx, 6U, &event), WL_ERR_NO_DATA);
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_SENDING);
  zassert_ok(wl_tx_complete(&ctx, cap.last_token, WL_OK));
  zassert_equal(wl_poll(&ctx, 12U, &event), WL_ERR_NO_DATA);
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_SENDING);
  zassert_ok(wl_tx_complete(&ctx, cap.last_token, WL_OK));
  zassert_ok(wl_poll(&ctx, 18U, &event));
  zassert_equal(event.type, WL_EVT_TX_TIMEOUT);
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_FAILED);
}

ZTEST(wirelink_protocol_unit, test_rejects_crc_broken_frame)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};

  wl_config_t cfg = {
    .max_payload_len = 64U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC32C,
    .session_id = 0xCAFEBABE11223344ULL,
    .max_retries = 0U,
    .ack_timeout_ms = 20U,
  };

  wl_sink_result_t script[] = {WL_SINK_SENT};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 1);

  wl_wire_packet_t packet = {
    .type = WL_PACKET_DATA,
    .integrity = WL_INTEGRITY_CRC32C,
    .flags = WL_PACKET_FLAG_RELIABLE,
    .message_id = 0x10U,
    .session_id = wl_ctx_impl(&ctx)->session_id,
    .sequence = 1U,
    .payload = NULL,
    .payload_len = 0U,
  };
  uint8_t wire[WL_FRAME_MAX_RAW_LEN];
  size_t wire_len = 0U;
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, wire,
                            sizeof(wire), &wire_len));
  wire[wire_len - 1U] ^= 0x01U;

  zassert_equal(wl_feed_unit(&ctx, wire, wire_len), WL_ERR_CRC);
}

ZTEST(wirelink_protocol_unit, test_rejects_receive_reserved_flags)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};
  wl_event_t event = {0};

  wl_config_t cfg = {
    .max_payload_len = 64U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_NONE,
    .session_id = 0xBEEF000000000001ULL,
    .max_retries = 0U,
    .ack_timeout_ms = 20U,
  };

  wl_sink_result_t script[] = {WL_SINK_FAILED};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 1);

  wl_wire_packet_t packet = {
    .type = WL_PACKET_DATA,
    .flags = WL_PACKET_FLAG_RELIABLE,
    .message_id = 0x11U,
    .session_id = wl_ctx_impl(&ctx)->session_id,
    .sequence = 1U,
    .payload = NULL,
    .payload_len = 0U,
    .integrity = WL_INTEGRITY_NONE,
  };

  uint8_t wire[WL_FRAME_MAX_RAW_LEN];
  size_t wire_len = 0U;
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, wire,
                            sizeof(wire), &wire_len));
  wire[5] |= WL_PACKET_FLAG_RESERVED_MASK;

  zassert_equal(wl_feed_unit(&ctx, wire, wire_len), WL_ERR_BAD_FRAME);
  zassert_equal(wl_poll(&ctx, 1U, &event), WL_ERR_NO_DATA);
}

ZTEST(wirelink_protocol_unit, test_rejects_receive_nack_and_payload_overflow)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};
  wl_event_t event = {0};
  const uint8_t payload[] = {1, 2, 3};

  wl_config_t cfg = {
    .max_payload_len = 2U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_NONE,
    .session_id = 0xBEEF000000000002ULL,
    .max_retries = 0U,
    .ack_timeout_ms = 20U,
  };

  wl_sink_result_t script[] = {WL_SINK_FAILED};
  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                   sizeof(tx_mem), script, 1);

  wl_wire_packet_t packet = {
    .type = WL_PACKET_DATA,
    .flags = 0U,
    .message_id = 0x12U,
    .session_id = wl_ctx_impl(&ctx)->session_id,
    .sequence = 1U,
    .payload = payload,
    .payload_len = sizeof(payload),
    .integrity = WL_INTEGRITY_NONE,
  };
  uint8_t wire[WL_FRAME_MAX_RAW_LEN];
  size_t wire_len = 0U;
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, wire,
                            sizeof(wire), &wire_len));
  zassert_equal(wl_feed_unit(&ctx, wire, wire_len), WL_ERR_PAYLOAD_TOO_LONG);
  zassert_equal(wl_poll(&ctx, 1U, &event), WL_ERR_NO_DATA);

  wire[4] = WL_PACKET_NACK;
  zassert_equal(wl_feed_unit(&ctx, wire, wire_len), WL_ERR_NOT_SUPPORTED);
  zassert_equal(wl_poll(&ctx, 2U, &event), WL_ERR_NO_DATA);
}

ZTEST(wirelink_protocol_unit, test_rx_event_borrows_payload_until_release)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[64];
  uint8_t tx_mem[128];
  struct test_sink_capture cap = {0};
  wl_event_t event = {0};
  const uint8_t payload[] = {0x11U, 0x22U, 0x33U};
  wl_config_t cfg = {
    .max_payload_len = 32U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_NONE,
    .session_id = 0xC0DEC0DEC0DEC0DEULL,
    .ack_timeout_ms = 20U,
  };
  wl_sink_result_t script[] = {WL_SINK_SENT};
  wl_wire_packet_t packet = {
    .type = WL_PACKET_DATA,
    .message_id = 0x55U,
    .session_id = 1U,
    .sequence = 1U,
    .payload = payload,
    .payload_len = sizeof(payload),
    .integrity = WL_INTEGRITY_NONE,
  };
  uint8_t wire[WL_FRAME_MAX_RAW_LEN];
  size_t wire_len = 0U;

  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                    sizeof(tx_mem), script, 1U);
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, wire,
                              sizeof(wire), &wire_len));
  zassert_ok(wl_feed_unit(&ctx, wire, wire_len));
  memset(wire, 0, wire_len);
  zassert_ok(wl_poll(&ctx, 1U, &event));
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  zassert_equal(wl_feed_unit(&ctx, wire, wire_len), WL_ERR_WOULD_BLOCK);
  wl_event_release(&ctx, &event);
  zassert_equal(wl_ctx_impl(&ctx)->rx_event_leased, 0U);
}

ZTEST(wirelink_protocol_unit, test_reliable_take_invalidates_generation_handle)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[64];
  uint8_t tx_mem[128];
  struct test_sink_capture cap = {0};
  wl_tx_handle_t first = 0U;
  wl_tx_handle_t second = 0U;
  wl_tx_result_t result = {0};
  uint8_t ack_wire[WL_FRAME_MAX_RAW_LEN];
  size_t ack_len = 0U;
  wl_config_t cfg = {
    .max_payload_len = 32U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_NONE,
    .session_id = 0xABCD000000000001ULL,
    .ack_timeout_ms = 20U,
  };
  wl_sink_result_t script[] = {WL_SINK_SENT, WL_SINK_SENT};
  wl_wire_packet_t ack = {
    .type = WL_PACKET_ACK,
    .session_id = cfg.session_id,
    .sequence = 0U,
    .integrity = WL_INTEGRITY_NONE,
  };

  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                    sizeof(tx_mem), script, 2U);
  zassert_ok(wl_send_reliable(&ctx, 1U, NULL, 0U, &first));
  zassert_ok(wl_frame_encode(&ack, WL_ENVELOPE_NATIVE_PACKET, ack_wire,
                              sizeof(ack_wire), &ack_len));
  zassert_ok(wl_feed_unit(&ctx, ack_wire, ack_len));
  zassert_ok(wl_tx_take(&ctx, first, &result));
  zassert_equal(result.state, WL_TX_STATE_SUCCESS);
  zassert_ok(wl_send_reliable(&ctx, 2U, NULL, 0U, &second));
  zassert_not_equal(first, second);
  zassert_equal(wl_tx_status(&ctx, first, &result.state), WL_ERR_NOT_FOUND);
}

ZTEST(wirelink_protocol_unit, test_cancel_queued_reliable_send)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[128];
  uint8_t tx_mem[128];
  struct test_sink_capture cap = {0};
  wl_tx_handle_t handle = 0U;
  wl_tx_result_t result = {0};
  wl_event_t event = {0};
  wl_config_t cfg = {
    .max_payload_len = 32U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC16,
    .session_id = UINT64_C(0xCA11000000000001),
    .max_retries = 2U,
    .ack_timeout_ms = 5U,
  };
  wl_sink_result_t script[] = {WL_SINK_BUSY, WL_SINK_SENT};

  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                    sizeof(tx_mem), script, ARRAY_SIZE(script));
  zassert_ok(wl_send_reliable(&ctx, 0x61U, NULL, 0U, &handle));
  zassert_equal(wl_ctx_impl(&ctx)->tx_queued, 1U);
  zassert_ok(wl_tx_cancel(&ctx, handle));
  zassert_equal(wl_ctx_impl(&ctx)->tx_queued, 0U);
  zassert_equal(wl_poll(&ctx, 100U, &event), WL_ERR_NO_DATA);
  zassert_equal(cap.call_count, 1U, "a cancelled queued send must not start");
  zassert_ok(wl_tx_take(&ctx, handle, &result));
  zassert_equal(result.state, WL_TX_STATE_CANCELLED);
  zassert_equal(result.result, WL_ERR_CANCELLED);
  zassert_equal(result.retries_used, 0U);
}

ZTEST(wirelink_protocol_unit, test_cancel_inflight_waits_for_io_completion)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[128];
  uint8_t tx_mem[128];
  struct test_sink_capture cap = {0};
  wl_tx_handle_t handle = 0U;
  wl_tx_result_t result = {0};
  wl_event_t event = {0};
  wl_config_t cfg = {
    .max_payload_len = 32U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC16,
    .session_id = UINT64_C(0xCA11000000000002),
    .max_retries = 2U,
    .ack_timeout_ms = 5U,
  };
  wl_sink_result_t script[] = {WL_SINK_STARTED};

  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                    sizeof(tx_mem), script, ARRAY_SIZE(script));
  zassert_ok(wl_send_reliable(&ctx, 0x62U, NULL, 0U, &handle));
  zassert_equal(wl_ctx_impl(&ctx)->tx_inflight, 1U);
  zassert_ok(wl_tx_cancel(&ctx, handle));
  zassert_equal(wl_tx_take(&ctx, handle, &result), WL_ERR_INVALID_STATE,
                "the transport still owns the in-flight unit");
  zassert_equal(wl_poll(&ctx, 100U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_tx_complete(&ctx, cap.last_token, WL_OK));
  zassert_ok(wl_tx_take(&ctx, handle, &result));
  zassert_equal(result.state, WL_TX_STATE_CANCELLED);
  zassert_equal(result.result, WL_ERR_CANCELLED);
  zassert_equal(cap.call_count, 1U, "completion must not restart a cancelled TX");
}

ZTEST(wirelink_protocol_unit, test_cancel_waiting_ack_ignores_late_ack)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[128];
  uint8_t tx_mem[128];
  uint8_t ack_wire[WL_FRAME_HEADER_SIZE + WL_FRAME_MAX_CRC];
  size_t ack_len = 0U;
  struct test_sink_capture cap = {0};
  wl_tx_handle_t handle = 0U;
  wl_tx_result_t result = {0};
  wl_event_t event = {0};
  wl_config_t cfg = {
    .max_payload_len = 32U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC32C,
    .session_id = UINT64_C(0xCA11000000000003),
    .max_retries = 2U,
    .ack_timeout_ms = 5U,
  };
  wl_sink_result_t script[] = {WL_SINK_SENT};
  wl_wire_packet_t ack = {
    .type = WL_PACKET_ACK,
    .integrity = WL_INTEGRITY_CRC32C,
    .session_id = cfg.session_id,
    .sequence = 0U,
  };

  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                    sizeof(tx_mem), script, ARRAY_SIZE(script));
  zassert_ok(wl_send_reliable(&ctx, 0x63U, NULL, 0U, &handle));
  zassert_equal(wl_ctx_impl(&ctx)->tx_state, WL_TX_STATE_WAITING_ACK);
  zassert_ok(wl_tx_cancel(&ctx, handle));
  zassert_ok(wl_frame_encode(&ack, WL_ENVELOPE_NATIVE_PACKET, ack_wire,
                             sizeof(ack_wire), &ack_len));
  zassert_ok(wl_feed_unit(&ctx, ack_wire, ack_len));
  zassert_equal(wl_poll(&ctx, 100U, &event), WL_ERR_NO_DATA,
                "a late ACK must not resurrect a cancelled transaction");
  zassert_ok(wl_tx_take(&ctx, handle, &result));
  zassert_equal(result.state, WL_TX_STATE_CANCELLED);
  zassert_equal(result.result, WL_ERR_CANCELLED);
}

ZTEST(wirelink_protocol_unit, test_ack_timeout_across_uint32_time_wrap)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[128];
  uint8_t tx_mem[128];
  struct test_sink_capture cap = {0};
  wl_tx_handle_t handle = 0U;
  wl_tx_result_t result = {0};
  wl_event_t event = {0};
  wl_config_t cfg = {
    .max_payload_len = 32U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET,
    .integrity = WL_INTEGRITY_CRC16,
    .session_id = UINT64_C(0x710E000000000001),
    .max_retries = 1U,
    .ack_timeout_ms = 5U,
  };
  wl_sink_result_t script[] = {WL_SINK_SENT, WL_SINK_SENT};
  const wl_time_ms_t before_wrap = UINT32_MAX - 2U;

  init_ctx_and_sink(&cap, &ctx, &cfg, 0U, rx_mem, sizeof(rx_mem), tx_mem,
                    sizeof(tx_mem), script, ARRAY_SIZE(script));
  zassert_equal(wl_poll(&ctx, before_wrap, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_send_reliable(&ctx, 0x64U, NULL, 0U, &handle));

  zassert_equal(wl_poll(&ctx, 1U, &event), WL_ERR_NO_DATA);
  zassert_equal(cap.call_count, 1U, "only four milliseconds have elapsed");
  zassert_equal(wl_poll(&ctx, 2U, &event), WL_ERR_NO_DATA);
  zassert_equal(cap.call_count, 2U, "the retry is due after wraparound");
  zassert_equal(wl_ctx_impl(&ctx)->tx_retries_used, 1U);

  zassert_equal(wl_poll(&ctx, 6U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_poll(&ctx, 7U, &event));
  zassert_equal(event.type, WL_EVT_TX_TIMEOUT);
  zassert_equal(event.handle, handle);
  zassert_ok(wl_tx_take(&ctx, handle, &result));
  zassert_equal(result.state, WL_TX_STATE_FAILED);
  zassert_equal(result.result, WL_ERR_TIMEOUT);
  zassert_equal(result.retries_used, 1U);
}

ZTEST_SUITE(wirelink_protocol_unit, NULL, NULL, NULL, NULL, NULL);
