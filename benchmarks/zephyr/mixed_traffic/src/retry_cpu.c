/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/wirelink.h"
#include "wirelink/frame.h"
#include "wirelink/cobs.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/timing/timing.h>

enum { IDLE_SEND, CACHED_RETRY, INTERLEAVE_SEND, INTERLEAVED_RETRY, MODES };
enum { MAX_PAYLOAD = 2048, UNIT = 2200, REPEATS = 5, OPERATIONS = 16 };
static struct {
  wl_ctx_t link;
  uint8_t payload[MAX_PAYLOAD], retained[MAX_PAYLOAD];
  uint8_t unit[UNIT], control[64], fifo[UNIT * 2], rx[UNIT], expected[UNIT];
  const uint8_t *sent;
  size_t sent_length, expected_length;
  unsigned submissions;
  int envelope;
  wl_tx_handle_t handle;
} fixture;

static wl_sink_result_t sink(void *context, wl_io_token_t token,
                              const uint8_t *bytes, size_t length) {
  ARG_UNUSED(context);
  ARG_UNUSED(token);
  fixture.sent = bytes;
  fixture.sent_length = length;
  ++fixture.submissions;
  return WL_SINK_SENT;
}

static void prepare(unsigned mode, int envelope, unsigned claim, size_t length) {
  const wl_config_t config = {.max_payload_len = MAX_PAYLOAD,
      .envelope = envelope, .integrity = WL_INTEGRITY_CRC32C, .session_id = 123U,
      .ack_timeout_ms = 100U, .max_retries = 2U};
  const wl_storage_t storage = {fixture.retained, sizeof(fixture.retained),
      fixture.unit, sizeof(fixture.unit), fixture.control, sizeof(fixture.control),
      fixture.fifo, sizeof(fixture.fifo), fixture.rx, sizeof(fixture.rx)};
  __ASSERT_NO_MSG(wl_init(&fixture.link, &config, &storage) == WL_OK);
  __ASSERT_NO_MSG(wl_set_sink(&fixture.link, sink, NULL) == WL_OK);
  fixture.submissions = 0U;
  fixture.envelope = envelope;
  for (size_t i = 0U; i < length; ++i) fixture.payload[i] = (uint8_t)(i * 29U);
  if (mode == IDLE_SEND) return;
  if (claim != 0U) {
    wl_tx_payload_claim_t space;
    __ASSERT_NO_MSG(wl_tx_payload_claim(&fixture.link, 21U, WL_DELIVERY_RELIABLE, &space) == WL_OK);
    memcpy(space.span.data, fixture.payload, length);
    __ASSERT_NO_MSG(wl_tx_payload_commit(&fixture.link, &space, length, 1U, &fixture.handle) == WL_OK);
  } else {
    __ASSERT_NO_MSG(wl_send_reliable(&fixture.link, 21U, fixture.payload,
                                     length, 1U, &fixture.handle) == WL_OK);
  }
  fixture.expected_length = fixture.sent_length;
  memcpy(fixture.expected, fixture.sent, fixture.expected_length);
  /* A retry must retain accepted bytes even after the caller changes its input. */
  memset(fixture.payload, 0xA5, length);
  if (mode == INTERLEAVED_RETRY) {
    const int status = wl_send_unreliable(&fixture.link, 23U, fixture.payload, length);
    __ASSERT_NO_MSG(status == (MIXED_EXPECT_COEXIST ? WL_OK : WL_ERR_BUSY));
    if (status == WL_OK) {
      wl_event_t event;
      __ASSERT_NO_MSG(wl_poll(&fixture.link, 2U, &event) == WL_OK);
      __ASSERT_NO_MSG(event.type == WL_EVT_TX_SUCCESS && event.handle == 0U);
    }
  }
}

static int operation(unsigned mode, size_t length) {
  if (mode == IDLE_SEND || mode == INTERLEAVE_SEND)
    return wl_send_unreliable(&fixture.link, 23U, fixture.payload, length);
  wl_event_t event;
  return wl_poll(&fixture.link, 101U, &event);
}

static void validate(unsigned mode, size_t length, int status) {
  if (mode == CACHED_RETRY || mode == INTERLEAVED_RETRY) {
    __ASSERT_NO_MSG(status == WL_ERR_NO_DATA);
    __ASSERT_NO_MSG(fixture.submissions ==
        (mode == INTERLEAVED_RETRY && MIXED_EXPECT_COEXIST ? 3U : 2U));
    __ASSERT_NO_MSG(fixture.sent_length == fixture.expected_length);
    __ASSERT_NO_MSG(memcmp(fixture.sent, fixture.expected, fixture.sent_length) == 0);
    wl_tx_result_t result;
    __ASSERT_NO_MSG(wl_tx_cancel(&fixture.link, fixture.handle) == WL_OK);
    __ASSERT_NO_MSG(wl_tx_take(&fixture.link, fixture.handle, &result) == WL_OK);
    __ASSERT_NO_MSG(result.retries_used == 1U);
  } else {
    const bool accepted = mode == IDLE_SEND || MIXED_EXPECT_COEXIST;
    __ASSERT_NO_MSG(status == (accepted ? WL_OK : WL_ERR_BUSY));
    __ASSERT_NO_MSG(fixture.submissions == (mode == IDLE_SEND ? 1U : accepted ? 2U : 1U));
    __ASSERT_NO_MSG(fixture.sent_length > length);
    if (accepted) {
      const uint8_t *raw = fixture.sent;
      size_t raw_length = fixture.sent_length;
      if (fixture.envelope == WL_ENVELOPE_COBS_STREAM) {
        __ASSERT_NO_MSG(raw[raw_length - 1U] == 0U);
        __ASSERT_NO_MSG(wl_cobs_decode(raw, raw_length - 1U, fixture.rx,
            sizeof(fixture.rx), &raw_length) == WL_OK);
        raw = fixture.rx;
      } else if (fixture.envelope == WL_ENVELOPE_BUS_LENGTH16) {
        raw += 2U;
        raw_length -= 2U;
      }
      wl_frame_view_t frame;
      __ASSERT_NO_MSG(wl_frame_decode(raw, raw_length, WL_INTEGRITY_CRC32C, &frame) == WL_OK);
      __ASSERT_NO_MSG(frame.message_id == 23U && frame.flags == 0U && frame.payload.length == length);
      __ASSERT_NO_MSG(memcmp(frame.payload.data, fixture.payload, length) == 0);
    }
  }
}

void mixed_retry_cpu(void) {
  const unsigned sizes[] = {32U, 512U, MAX_PAYLOAD};
  for (unsigned mode = 0U; mode < MODES; ++mode)
    for (int envelope = WL_ENVELOPE_COBS_STREAM; envelope <= WL_ENVELOPE_BUS_LENGTH16; ++envelope)
      for (unsigned claim = 0U; claim < 2U; ++claim)
        for (unsigned size = 0U; size < ARRAY_SIZE(sizes); ++size) {
          const unsigned length = sizes[size];
          for (unsigned warm = 0U; warm < 8U; ++warm) {
            prepare(mode, envelope, claim, length);
            validate(mode, length, operation(mode, length));
          }
          for (unsigned repeat = 0U; repeat < REPEATS; ++repeat) {
            uint64_t total = 0U, maximum = 0U;
            for (unsigned i = 0U; i < OPERATIONS; ++i) {
              prepare(mode, envelope, claim, length);
              const unsigned key = irq_lock();
              timing_t begin = timing_counter_get();
              compiler_barrier();
              const int result = operation(mode, length);
              compiler_barrier();
              timing_t end = timing_counter_get();
              irq_unlock(key);
              const uint64_t cycles = timing_cycles_get(&begin, &end);
              total += cycles;
              if (cycles > maximum) maximum = cycles;
              validate(mode, length, result);
            }
            printk("mixed_retry_v1,m=%u,e=%d,claim=%u,b=%u,r=%u,n=%u,cycles=%llu,max=%llu\n",
                mode, envelope, claim, length, repeat, OPERATIONS,
                (unsigned long long)total, (unsigned long long)maximum);
          }
          k_sleep(K_MSEC(10));
        }
}
