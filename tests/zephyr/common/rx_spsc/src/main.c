/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#if defined(CONFIG_ARCH_POSIX)
#include <stdatomic.h>

#include <zephyr/kernel.h>
#endif

#include "wirelink/cobs.h"
#include "wirelink/frame.h"
#include "wirelink/wirelink.h"
#include "context.h"

#define TEST_MAX_PAYLOAD 64U
#define TEST_RX_STORAGE 128U
#define TEST_TX_STORAGE 128U

struct sink_capture {
  size_t calls;
};

struct rx_fixture {
  wl_ctx_t ctx;
  wl_config_t config;
  wl_storage_requirements_t requirements;
  wl_storage_t storage;
  struct sink_capture sink;
  uint8_t tx_payload[TEST_MAX_PAYLOAD];
  uint8_t tx_unit[TEST_TX_STORAGE];
  uint8_t control_unit[TEST_TX_STORAGE];
  uint8_t rx_fifo[TEST_RX_STORAGE];
  uint8_t rx_fallback[TEST_RX_STORAGE];
};

static wl_sink_result_t capture_sink(void *user_data, wl_io_token_t token,
                                     const uint8_t *data, size_t len) {
  struct sink_capture *capture = user_data;

  (void)token;
  (void)data;
  (void)len;
  capture->calls++;
  return WL_SINK_SENT;
}

static void init_fixture(struct rx_fixture *fixture) {
  memset(fixture, 0, sizeof(*fixture));
  fixture->config = (wl_config_t){
      .max_payload_len = TEST_MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .integrity = WL_INTEGRITY_NONE,
      .session_id = UINT64_C(0x123456789ABCDEF0),
      .max_retries = 1U,
      .ack_timeout_ms = 20U,
  };
  zassert_ok(wl_config_requirements(&fixture->config, &fixture->requirements));
  zassert_true(fixture->requirements.tx_payload_size <=
               sizeof(fixture->tx_payload));
  zassert_true(fixture->requirements.tx_unit_size <= sizeof(fixture->tx_unit));
  zassert_true(fixture->requirements.control_unit_size <=
               sizeof(fixture->control_unit));
  zassert_true(fixture->requirements.rx_fifo_size <= sizeof(fixture->rx_fifo));
  zassert_true(fixture->requirements.rx_fallback_size <=
               sizeof(fixture->rx_fallback));

  fixture->storage = (wl_storage_t){
      .tx_payload = fixture->tx_payload,
      .tx_payload_size = fixture->requirements.tx_payload_size,
      .tx_unit = fixture->tx_unit,
      .tx_unit_size = fixture->requirements.tx_unit_size,
      .control_unit = fixture->control_unit,
      .control_unit_size = fixture->requirements.control_unit_size,
      .rx_fifo = fixture->rx_fifo,
      .rx_fifo_size = fixture->requirements.rx_fifo_size,
      .rx_fallback = fixture->rx_fallback,
      .rx_fallback_size = fixture->requirements.rx_fallback_size,
  };
  zassert_ok(wl_init(&fixture->ctx, &fixture->config, &fixture->storage));
  zassert_ok(wl_set_sink(&fixture->ctx, capture_sink, &fixture->sink));
}

static size_t encode_data(const struct rx_fixture *fixture, uint8_t flags,
                          uint16_t message_id, uint32_t sequence,
                          const uint8_t *payload, size_t payload_len,
                          uint8_t *output, size_t output_size) {
  wl_wire_packet_t packet = {
      .type = WL_PACKET_DATA,
      .integrity = fixture->config.integrity,
      .flags = flags,
      .message_id = message_id,
      .session_id = UINT64_C(0x0FEDCBA987654321),
      .sequence = sequence,
      .payload = payload,
      .payload_len = payload_len,
  };
  size_t output_len = 0U;

  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM, output,
                             output_size, &output_len));
  return output_len;
}

static size_t usable_rx_capacity(const struct rx_fixture *fixture) {
  return wl_frame_encode_overhead(WL_ENVELOPE_COBS_STREAM,
                                  fixture->config.integrity) +
         fixture->config.max_payload_len;
}

static bool points_into(const uint8_t *pointer, const uint8_t *storage,
                        size_t storage_size) {
  uintptr_t address = (uintptr_t)pointer;
  uintptr_t begin = (uintptr_t)storage;

  return address >= begin && address < begin + storage_size;
}

ZTEST(wirelink_rx_spsc, test_feed_only_enqueues_until_poll) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  const uint8_t payload[] = {0x10U, 0x00U, 0x20U};
  size_t accepted = SIZE_MAX;
  size_t wire_len;
  wl_event_t event = {0};

  init_fixture(&fixture);
  wire_len = encode_data(&fixture, WL_PACKET_FLAG_RELIABLE, 0x102U, 7U, payload,
                         sizeof(payload), wire, sizeof(wire));

  zassert_ok(wl_feed_bytes(&fixture.ctx, wire, wire_len, &accepted));
  zassert_equal(accepted, wire_len);
  zassert_equal(fixture.sink.calls, 0U, "producer path must not emit an ACK");
  zassert_equal(wl_ctx_impl(&fixture.ctx)->has_event, 0U,
                "producer path must not parse or publish an event");
  zassert_equal(wl_ctx_impl(&fixture.ctx)->control_pending, 0U);

  zassert_ok(wl_poll(&fixture.ctx, 1U, &event));
  zassert_equal(event.type, WL_EVT_RELIABLE_RX);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  zassert_equal(fixture.sink.calls, 1U, "poll must submit the ACK");
  wl_event_release(&fixture.ctx, &event);
}

ZTEST(wirelink_rx_spsc, test_contiguous_payload_is_borrowed_from_ring) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  const uint8_t payload[] = {0x00U, 0x11U, 0x22U, 0x00U, 0x33U};
  size_t accepted = 0U;
  size_t wire_len;
  wl_event_t event = {0};

  init_fixture(&fixture);
  wire_len = encode_data(&fixture, 0U, 0x201U, 1U, payload, sizeof(payload),
                         wire, sizeof(wire));
  zassert_ok(wl_feed_bytes(&fixture.ctx, wire, wire_len, &accepted));
  memset(wire, 0xA5, wire_len);

  zassert_ok(wl_poll(&fixture.ctx, 2U, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  zassert_equal(event.peer_session_id, 0U);
  zassert_true(points_into(event.payload, fixture.rx_fifo,
                           fixture.storage.rx_fifo_size));
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  zassert_equal(wl_ctx_impl(&fixture.ctx)->rx_event_leased, 1U);
  wl_event_release(&fixture.ctx, &event);
  zassert_equal(wl_ctx_impl(&fixture.ctx)->rx_event_leased, 0U);
}

ZTEST(wirelink_rx_spsc, test_wrapped_frame_uses_fallback) {
  struct rx_fixture fixture;
  uint8_t first[TEST_RX_STORAGE];
  uint8_t wrapped[TEST_RX_STORAGE];
  uint8_t payload[TEST_MAX_PAYLOAD];
  size_t accepted = 0U;
  size_t first_len;
  size_t wrapped_len;
  size_t capacity;
  size_t prefix_len;
  wl_event_t event = {0};

  for (size_t i = 0U; i < sizeof(payload); ++i) {
    payload[i] = (uint8_t)(i * 3U);
  }
  init_fixture(&fixture);
  first_len =
      encode_data(&fixture, 0U, 0x301U, 1U, NULL, 0U, first, sizeof(first));
  wrapped_len = encode_data(&fixture, WL_PACKET_FLAG_RELIABLE, 0x302U, 2U,
                            payload, sizeof(payload), wrapped,
                            sizeof(wrapped));
  capacity = usable_rx_capacity(&fixture);
  zassert_equal(wrapped_len, capacity,
                "maximum test frame must fill the usable ring");
  zassert_true(first_len < capacity);
  prefix_len = capacity - first_len;

  zassert_ok(wl_feed_bytes(&fixture.ctx, first, first_len, &accepted));
  zassert_ok(wl_feed_bytes(&fixture.ctx, wrapped, prefix_len, &accepted));
  zassert_ok(wl_poll(&fixture.ctx, 3U, &event));
  zassert_equal(event.message_id, 0x301U);
  wl_event_release(&fixture.ctx, &event);

  zassert_ok(wl_feed_bytes(&fixture.ctx, wrapped + prefix_len,
                           wrapped_len - prefix_len, &accepted));
  event = (wl_event_t){0};
  zassert_ok(wl_poll(&fixture.ctx, 4U, &event));
  zassert_equal(event.message_id, 0x302U);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  zassert_true(points_into(event.payload, fixture.rx_fallback,
                           fixture.storage.rx_fallback_size));
  zassert_false(points_into(event.payload, fixture.rx_fifo,
                            fixture.storage.rx_fifo_size));
  wl_event_release(&fixture.ctx, &event);
}

ZTEST(wirelink_rx_spsc, test_held_event_prevents_borrowed_bytes_overwrite) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  uint8_t filler[TEST_RX_STORAGE];
  const uint8_t payload[] = {0xA1U, 0x00U, 0xB2U, 0xC3U};
  size_t accepted = 0U;
  size_t wire_len;
  size_t capacity;
  wl_event_t event = {0};
  wl_rx_counters_t counters = {0};

  memset(filler, 0x5AU, sizeof(filler));
  init_fixture(&fixture);
  wire_len = encode_data(&fixture, 0U, 0x401U, 1U, payload, sizeof(payload),
                         wire, sizeof(wire));
  capacity = usable_rx_capacity(&fixture);
  zassert_ok(wl_feed_bytes(&fixture.ctx, wire, wire_len, &accepted));
  zassert_ok(wl_poll(&fixture.ctx, 5U, &event));
  zassert_true(points_into(event.payload, fixture.rx_fifo,
                           fixture.storage.rx_fifo_size));

  zassert_ok(
      wl_feed_bytes(&fixture.ctx, filler, capacity - wire_len, &accepted));
  zassert_equal(accepted, capacity - wire_len);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  zassert_equal(wl_feed_bytes(&fixture.ctx, filler, 1U, &accepted),
                WL_ERR_WOULD_BLOCK);
  zassert_equal(accepted, 0U);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&fixture.ctx, &event);
  zassert_equal(wl_poll(&fixture.ctx, 6U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_rx_get_counters(&fixture.ctx, &counters));
  zassert_equal(counters.overflow, 1U,
                "consumer must account for producer backpressure");
}

ZTEST(wirelink_rx_spsc, test_reserve_short_commit_and_double_reserve) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  const uint8_t payload[] = {0x12U, 0x00U, 0x34U};
  const size_t first_chunk = 5U;
  size_t accepted = 0U;
  size_t wire_len;
  wl_span_t reservation = {0};
  wl_span_t second = {0};
  wl_event_t event = {0};

  init_fixture(&fixture);
  wire_len = encode_data(&fixture, 0U, 0x501U, 1U, payload, sizeof(payload),
                         wire, sizeof(wire));
  zassert_ok(wl_rx_reserve(&fixture.ctx, &reservation));
  zassert_true(reservation.length >= first_chunk);
  memcpy(reservation.data, wire, first_chunk);
  zassert_equal(wl_rx_reserve(&fixture.ctx, &second), WL_ERR_INVALID_STATE);
  zassert_ok(wl_rx_commit(&fixture.ctx, first_chunk));
  zassert_equal(wl_rx_commit(&fixture.ctx, 0U), WL_ERR_INVALID_STATE);

  zassert_ok(wl_feed_bytes(&fixture.ctx, wire + first_chunk,
                           wire_len - first_chunk, &accepted));
  zassert_equal(accepted, wire_len - first_chunk);
  zassert_ok(wl_poll(&fixture.ctx, 6U, &event));
  zassert_equal(event.message_id, 0x501U);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&fixture.ctx, &event);
}

ZTEST(wirelink_rx_spsc, test_dma_claims_publish_in_order_and_decode) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  const uint8_t payload[] = {0xABU, 0x00U, 0xCDU};
  wl_rx_dma_claim_t first = {0};
  wl_rx_dma_claim_t second = {0};
  wl_rx_dma_claim_t third = {0};
  wl_event_t event = {0};
  size_t wire_len;
  const size_t first_length = 7U;

  init_fixture(&fixture);
  wire_len = encode_data(&fixture, 0U, 0x511U, 1U, payload, sizeof(payload),
                         wire, sizeof(wire));
  zassert_true(wire_len > first_length);
  zassert_ok(wl_rx_dma_claim(&fixture.ctx, first_length, &first));
  zassert_equal(first.span.length, first_length);
  zassert_ok(wl_rx_dma_claim(&fixture.ctx, wire_len - first_length, &second));
  zassert_equal(wl_rx_dma_claim(&fixture.ctx, 1U, &third),
                WL_ERR_WOULD_BLOCK);

  memcpy(first.span.data, wire, first_length);
  memcpy(second.span.data, wire + first_length, wire_len - first_length);
  zassert_ok(wl_rx_dma_publish(&fixture.ctx, &first, 0U, 3U));
  zassert_equal(wl_rx_dma_publish(&fixture.ctx, &second, 0U, 1U),
                WL_ERR_INVALID_STATE);
  zassert_ok(wl_rx_dma_publish(&fixture.ctx, &first, 3U, first_length - 3U));
  zassert_ok(wl_rx_dma_finish(&fixture.ctx, &first));
  zassert_ok(
      wl_rx_dma_publish(&fixture.ctx, &second, 0U, wire_len - first_length));
  zassert_ok(wl_rx_dma_finish(&fixture.ctx, &second));

  zassert_ok(wl_poll(&fixture.ctx, 7U, &event));
  zassert_equal(event.message_id, 0x511U);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&fixture.ctx, &event);
}

ZTEST(wirelink_rx_spsc, test_dma_abort_discards_partial_stream_before_resume) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  const uint8_t payload[] = {0xA5U, 0x5AU};
  wl_rx_dma_claim_t claim = {0};
  wl_event_t event = {0};
  wl_rx_counters_t counters = {0};
  size_t accepted = 0U;
  size_t wire_len;

  init_fixture(&fixture);
  zassert_ok(wl_rx_dma_claim(&fixture.ctx, 8U, &claim));
  memset(claim.span.data, 0xA5, 4U);
  zassert_ok(wl_rx_dma_publish(&fixture.ctx, &claim, 0U, 4U));
  zassert_ok(wl_rx_dma_abort(&fixture.ctx));
  zassert_equal(wl_poll(&fixture.ctx, 8U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_rx_get_counters(&fixture.ctx, &counters));
  zassert_equal(counters.overflow, 1U);

  wire_len = encode_data(&fixture, 0U, 0x512U, 1U, payload, sizeof(payload),
                         wire, sizeof(wire));
  zassert_ok(wl_feed_bytes(&fixture.ctx, wire, wire_len, &accepted));
  zassert_equal(accepted, wire_len);
  zassert_ok(wl_poll(&fixture.ctx, 9U, &event));
  zassert_equal(event.message_id, 0x512U);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&fixture.ctx, &event);
}

ZTEST(wirelink_rx_spsc, test_dma_finish_reclaims_short_final_claim) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  const uint8_t payload[] = {0x35U, 0x46U, 0x57U};
  wl_rx_dma_claim_t claim = {0};
  wl_rx_dma_claim_t successor = {0};
  wl_event_t event = {0};
  size_t wire_len;

  init_fixture(&fixture);
  wire_len = encode_data(&fixture, 0U, 0x513U, 1U, payload, sizeof(payload),
                         wire, sizeof(wire));
  zassert_ok(wl_rx_dma_claim(&fixture.ctx, wire_len + 8U, &claim));
  memcpy(claim.span.data, wire, wire_len);
  zassert_ok(wl_rx_dma_publish(&fixture.ctx, &claim, 0U, wire_len));
  zassert_ok(wl_rx_dma_finish(&fixture.ctx, &claim));

  zassert_ok(wl_poll(&fixture.ctx, 10U, &event));
  zassert_equal(event.message_id, 0x513U);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&fixture.ctx, &event);

  zassert_ok(wl_rx_dma_claim(&fixture.ctx, 8U, &claim));
  zassert_ok(wl_rx_dma_claim(&fixture.ctx, 8U, &successor));
  memset(claim.span.data, 0xA5, 4U);
  zassert_ok(wl_rx_dma_publish(&fixture.ctx, &claim, 0U, 4U));
  zassert_equal(wl_rx_dma_finish(&fixture.ctx, &claim), WL_ERR_INVALID_STATE);
  zassert_ok(wl_rx_dma_abort(&fixture.ctx));
}

ZTEST(wirelink_rx_spsc, test_dma_short_claim_can_restart_from_empty_ring) {
  struct rx_fixture fixture;
  uint8_t wire[TEST_RX_STORAGE];
  const uint8_t payloads[][4] = {
      {0x11U, 0x00U, 0x22U, 0x33U},
      {0x44U, 0x55U, 0x00U, 0x66U},
      {0x77U, 0x88U, 0x99U, 0x00U},
  };

  init_fixture(&fixture);
  for (size_t i = 0U; i < ARRAY_SIZE(payloads); ++i) {
    wl_rx_dma_claim_t claim = {0};
    wl_event_t event = {0};
    size_t wire_len =
        encode_data(&fixture, 0U, 0x514U, (uint32_t)(i + 1U), payloads[i],
                    sizeof(payloads[i]), wire, sizeof(wire));

    zassert_ok(
        wl_rx_dma_claim(&fixture.ctx, usable_rx_capacity(&fixture), &claim));
    zassert_equal(claim.span.length, usable_rx_capacity(&fixture));
    memcpy(claim.span.data, wire, wire_len);
    zassert_ok(wl_rx_dma_publish(&fixture.ctx, &claim, 0U, wire_len));
    zassert_ok(wl_rx_dma_finish(&fixture.ctx, &claim));
    zassert_ok(wl_poll(&fixture.ctx, (wl_time_ms_t)(11U + i), &event));
    zassert_equal(event.message_id, 0x514U);
    zassert_mem_equal(event.payload, payloads[i], sizeof(payloads[i]));
    wl_event_release(&fixture.ctx, &event);
  }
}

ZTEST(wirelink_rx_spsc, test_full_buffer_reports_partial_acceptance) {
  struct rx_fixture fixture;
  uint8_t input[TEST_RX_STORAGE + 8U];
  size_t accepted = SIZE_MAX;
  size_t capacity;
  wl_event_t event = {0};
  wl_rx_counters_t counters = {0};

  memset(input, 0x7EU, sizeof(input));
  init_fixture(&fixture);
  capacity = usable_rx_capacity(&fixture);

  zassert_equal(wl_feed_bytes(&fixture.ctx, input, capacity + 3U, &accepted),
                WL_ERR_WOULD_BLOCK);
  zassert_equal(accepted, capacity);
  zassert_equal(wl_feed_bytes(&fixture.ctx, input, 1U, &accepted),
                WL_ERR_WOULD_BLOCK);
  zassert_equal(accepted, 0U);
  zassert_equal(wl_poll(&fixture.ctx, 7U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_rx_get_counters(&fixture.ctx, &counters));
  zassert_equal(counters.overflow, 2U);
  zassert_ok(wl_feed_bytes(&fixture.ctx, input, 1U, &accepted));
  zassert_equal(accepted, 1U);
}

ZTEST(wirelink_rx_spsc, test_cobs_decode_in_place) {
  const uint8_t original[] = {0x00U, 0x11U, 0x22U, 0x00U,
                              0x33U, 0x00U, 0x00U, 0x44U};
  uint8_t buffer[32];
  size_t encoded_len = 0U;
  size_t decoded_len = 0U;

  zassert_ok(wl_cobs_encode(original, sizeof(original), buffer, sizeof(buffer),
                            &encoded_len));
  zassert_ok(wl_cobs_decode_in_place(buffer, encoded_len, &decoded_len));
  zassert_equal(decoded_len, sizeof(original));
  zassert_mem_equal(buffer, original, sizeof(original));
}

#if defined(CONFIG_ARCH_POSIX)

#define SPSC_STRESS_FRAMES 2000U
#define SPSC_STRESS_WINDOW 2U
#define SPSC_STRESS_STACK_SIZE 4096U

struct spsc_stress_state {
  struct rx_fixture *fixture;
  atomic_uint produced;
  atomic_uint consumed;
  atomic_uint done;
  atomic_int error;
};

static struct k_thread spsc_producer_thread;
K_THREAD_STACK_DEFINE(spsc_producer_stack, SPSC_STRESS_STACK_SIZE);

static void spsc_producer_entry(void *arg1, void *arg2, void *arg3) {
  struct spsc_stress_state *state = arg1;

  (void)arg2;
  (void)arg3;
  for (uint32_t sequence = 0U; sequence < SPSC_STRESS_FRAMES; ++sequence) {
    uint8_t payload[] = {(uint8_t)(sequence >> 24U), (uint8_t)(sequence >> 16U),
                         (uint8_t)(sequence >> 8U), (uint8_t)sequence};
    uint8_t wire[TEST_RX_STORAGE];
    wl_wire_packet_t packet = {
        .type = WL_PACKET_DATA,
        .integrity = state->fixture->config.integrity,
        .message_id = 0x701U,
        .session_id = UINT64_C(0x0FEDCBA987654321),
        .sequence = sequence,
        .payload = payload,
        .payload_len = sizeof(payload),
    };
    size_t wire_len = 0U;
    size_t accepted = 0U;
    int ret;

    while (sequence -
               atomic_load_explicit(&state->consumed, memory_order_relaxed) >=
           SPSC_STRESS_WINDOW) {
      k_yield();
    }
    ret = wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM, wire, sizeof(wire),
                          &wire_len);
    if (ret == WL_OK) {
      ret = wl_feed_bytes(&state->fixture->ctx, wire, wire_len, &accepted);
    }
    if (ret != WL_OK || accepted != wire_len) {
      atomic_store_explicit(&state->error,
                            ret != WL_OK ? ret : WL_ERR_INVALID_STATE,
                            memory_order_relaxed);
      atomic_store_explicit(&state->done, 1U, memory_order_release);
      return;
    }
    atomic_store_explicit(&state->produced, sequence + 1U,
                          memory_order_relaxed);
  }
  atomic_store_explicit(&state->done, 1U, memory_order_release);
}

ZTEST(wirelink_rx_spsc, test_threaded_sustained_producer_consumer_stress) {
  struct rx_fixture fixture;
  struct spsc_stress_state state = {.fixture = &fixture};

  init_fixture(&fixture);
  atomic_init(&state.produced, 0U);
  atomic_init(&state.consumed, 0U);
  atomic_init(&state.done, 0U);
  atomic_init(&state.error, WL_OK);

  (void)k_thread_create(&spsc_producer_thread, spsc_producer_stack,
                        K_THREAD_STACK_SIZEOF(spsc_producer_stack),
                        spsc_producer_entry, &state, NULL, NULL,
                        k_thread_priority_get(k_current_get()), 0U, K_NO_WAIT);

  for (uint32_t expected = 0U; expected < SPSC_STRESS_FRAMES; ++expected) {
    wl_event_t event = {0};
    const uint8_t expected_payload[] = {
        (uint8_t)(expected >> 24U), (uint8_t)(expected >> 16U),
        (uint8_t)(expected >> 8U), (uint8_t)expected};
    int ret;

    do {
      ret = wl_poll(&fixture.ctx, expected, &event);
      if (ret == WL_ERR_NO_DATA) {
        const unsigned int producer_done =
            atomic_load_explicit(&state.done, memory_order_acquire);

        if (producer_done != 0U) {
          zassert_equal(
              atomic_load_explicit(&state.error, memory_order_relaxed), WL_OK,
              "producer failed at frame %u", expected);
        }
        zassert_false(producer_done != 0U,
                      "producer stopped before frame %u became visible",
                      expected);
        k_yield();
      }
    } while (ret == WL_ERR_NO_DATA);
    zassert_ok(ret, "committed frame %u was not visible", expected);
    zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
    zassert_equal(event.message_id, 0x701U);
    zassert_equal(event.payload_len, sizeof(expected_payload));
    zassert_mem_equal(event.payload, expected_payload,
                      sizeof(expected_payload));
    wl_event_release(&fixture.ctx, &event);
    atomic_store_explicit(&state.consumed, expected + 1U,
                          memory_order_relaxed);
  }

  zassert_ok(k_thread_join(&spsc_producer_thread, K_SECONDS(5)));
  zassert_equal(atomic_load_explicit(&state.produced, memory_order_relaxed),
                SPSC_STRESS_FRAMES);
  zassert_equal(fixture.sink.calls, 0U,
                "unreliable RX must never invoke the TX sink");
}

#endif /* CONFIG_ARCH_POSIX */

ZTEST_SUITE(wirelink_rx_spsc, NULL, NULL, NULL, NULL, NULL);
