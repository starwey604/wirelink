/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wirelink/wirelink.h"

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

  zassert_ok(wl_init(ctx, cfg, (uint8_t *)rx_mem, rx_len,
                     (uint8_t *)tx_mem, tx_len));
  zassert_ok(wl_set_sink(ctx, test_sink_fn, cap));
}

ZTEST(wirelink_protocol_unit, test_busy_on_send_is_would_block)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};

  wl_config_t cfg = {
    .rx_buf_size = sizeof(rx_mem),
    .tx_buf_size = sizeof(tx_mem),
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

  zassert_equal(wl_send_unreliable(&ctx, 1U, (const uint8_t *)"", 0U),
               WL_ERR_WOULD_BLOCK);
  zassert_equal(ctx.tx_state, WL_TX_STATE_IDLE);
}

ZTEST(wirelink_protocol_unit, test_reliable_send_busy_does_not_occupy_tx_slot)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};

  wl_config_t cfg = {
    .rx_buf_size = sizeof(rx_mem),
    .tx_buf_size = sizeof(tx_mem),
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
  zassert_equal(wl_send_reliable(&ctx, 3U, (const uint8_t *)"B", 1U, &handle),
               WL_ERR_WOULD_BLOCK);
  zassert_equal(handle, 0U);
  zassert_equal(ctx.tx_state, WL_TX_STATE_IDLE);
}

ZTEST(wirelink_protocol_unit, test_reliable_send_started_and_complete)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};
  wl_tx_handle_t handle = 0;

  wl_config_t cfg = {
    .rx_buf_size = sizeof(rx_mem),
    .tx_buf_size = sizeof(tx_mem),
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
  zassert_equal(handle, 1U);
  zassert_equal(ctx.tx_state, WL_TX_STATE_SENDING);
  zassert_equal(ctx.tx_token, 2U);

  zassert_ok(wl_tx_complete(&ctx, cap.last_token, WL_OK));
  zassert_equal(ctx.tx_state, WL_TX_STATE_WAITING_ACK);
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
    .rx_buf_size = sizeof(rx_mem),
    .tx_buf_size = sizeof(tx_mem),
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
  zassert_equal(ctx.tx_state, WL_TX_STATE_WAITING_ACK);
  zassert_equal(handle, 1U);

  wl_wire_packet_t ack_packet = {
    .type = WL_PACKET_ACK,
    .integrity = WL_INTEGRITY_CRC32C,
    .flags = 0U,
    .cmd_id = 0U,
    .session_id = ctx.session_id,
    .sequence = 0U,
    .payload = NULL,
    .payload_len = 0U,
  };
  ack_packet.sequence = ctx.tx_waiting_seq;

  uint8_t ack_wire[WL_FRAME_MAX_RAW_LEN];
  size_t ack_len = 0U;
  zassert_ok(wl_frame_encode(&ack_packet, WL_ENVELOPE_NATIVE_PACKET, ack_wire,
                            sizeof(ack_wire), &ack_len));

  zassert_ok(wl_feed_unit(&ctx, ack_wire, ack_len));
  zassert_ok(wl_poll(&ctx, 0U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  zassert_equal(event.handle, handle);
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
    .rx_buf_size = sizeof(rx_mem),
    .tx_buf_size = sizeof(tx_mem),
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
  zassert_equal(ctx.tx_state, WL_TX_STATE_WAITING_ACK);

  zassert_equal(wl_poll(&ctx, 6U, &event), WL_ERR_NO_DATA);
  zassert_equal(wl_poll(&ctx, 12U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_poll(&ctx, 18U, &event));
  zassert_equal(event.type, WL_EVT_TX_TIMEOUT);
}

ZTEST(wirelink_protocol_unit, test_rejects_crc_broken_frame)
{
  wl_ctx_t ctx = {0};
  uint8_t rx_mem[256];
  uint8_t tx_mem[256];
  struct test_sink_capture cap = {0};

  wl_config_t cfg = {
    .rx_buf_size = sizeof(rx_mem),
    .tx_buf_size = sizeof(tx_mem),
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
    .cmd_id = 0x10U,
    .session_id = ctx.session_id,
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

ZTEST_SUITE(wirelink_protocol_unit, NULL, NULL, NULL, NULL, NULL);
