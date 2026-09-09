/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <zephyr/ztest.h>
#include "wirelink/endpoint.h"
#include "wirelink/port.h"

struct fixture {
  wl_endpoint_t endpoint;
  wl_time_ms_t time;
  unsigned reads, sends, events;
  wl_io_token_t token;
  uint8_t async, finish;
  uint8_t payload[128], tx[128], control[128], rx[128];
};

static struct fixture first, second;

static unsigned policy_events, policy_passes;
static void observe_event(void *context, wl_ctx_t *link,
                          const wl_event_t *ev, wl_time_ms_t now) {
  struct fixture *f = context;
  (void)link;
  (void)ev;
  zassert_equal(f->events, policy_events++); /* Before generated dispatch. */
  wl_time_ms_t snapshot;
  zassert_ok(wl_endpoint_now(&f->endpoint, &snapshot));
  zassert_equal(snapshot, now);
  zassert_equal(wl_endpoint_set_policy(&f->endpoint, NULL), WL_ERR_REENTRANT);
}
static uint8_t policy_progress(void *context, wl_ctx_t *link, wl_time_ms_t now) {
  (void)context;
  (void)link;
  (void)now;
  ++policy_passes;
  return 0U;
}
static uint32_t policy_deadline(const void *context, wl_time_ms_t now) {
  (void)context;
  (void)now;
  return 3U;
}

static wl_time_ms_t read_clock(void *user_data) {
  struct fixture *f = user_data;
  ++f->reads;
  return f->time;
}

static wl_sink_result_t sink(void *user_data, wl_io_token_t token,
                             const uint8_t *data, size_t size) {
  struct fixture *f = user_data;
  (void)data;
  (void)size;
  ++f->sends;
  f->token = token;
  return f->async ? WL_SINK_STARTED : WL_SINK_SENT;
}

static int service(void *user_data) {
  struct fixture *f = user_data;
  if (!f->finish) return WL_ERR_NO_DATA;
  f->finish = 0;
  return wl_tx_complete(wl_endpoint_link(&f->endpoint), f->token, WL_OK);
}

static wl_pump_event_disposition_t event(void *user_data, wl_ctx_t *ctx,
    const wl_event_t *ev, wl_time_ms_t now_ms) {
  struct fixture *f = user_data;
  wl_time_ms_t nested;
  unsigned reads = f->reads;
  (void)ctx;
  (void)ev;
  ++f->events;
  /* Advancing the source during a callback must not change this pass. */
  ++f->time;
  zassert_ok(wl_endpoint_now(&f->endpoint, &nested));
  zassert_equal(nested, now_ms);
  zassert_equal(f->reads, reads);
  zassert_equal(wl_endpoint_step(&f->endpoint, 1U), WL_ERR_REENTRANT);
  return WL_PUMP_EVENT_UNHANDLED;
}

static void init(struct fixture *f, wl_time_ms_t now_ms) {
  memset(f, 0, sizeof(*f));
  f->time = now_ms;
  const wl_config_t config = {.max_payload_len = 32U,
    .envelope = WL_ENVELOPE_NATIVE_PACKET, .integrity = WL_INTEGRITY_CRC32C,
    .session_id = 1U, .max_retries = 1U, .ack_timeout_ms = 5U};
  const wl_storage_t storage = {
    .tx_payload = f->payload, .tx_payload_size = sizeof(f->payload),
    .tx_unit = f->tx, .tx_unit_size = sizeof(f->tx),
    .control_unit = f->control, .control_unit_size = sizeof(f->control),
    .rx_fallback = f->rx, .rx_fallback_size = sizeof(f->rx)};
  wl_clock_t clock = {read_clock, f};
  const wl_pump_hooks_t app = {.application_user_data = f, .on_event = event};
  const wl_pump_hooks_t adapter = {.adapter_user_data = f, .service = service};
  zassert_equal(wl_endpoint_init(&f->endpoint, &config, &storage, NULL, &app),
                WL_ERR_INVALID_ARG);
  zassert_equal(wl_endpoint_init(&f->endpoint, &config, &storage,
                                 &(wl_clock_t){0}, &app), WL_ERR_INVALID_ARG);
  zassert_ok(wl_endpoint_init(&f->endpoint, &config, &storage, &clock, &app));
  memset(&clock, 0, sizeof(clock)); /* Descriptor lifetime is not borrowed. */
  zassert_ok(wl_endpoint_attach(&f->endpoint, &adapter));
  zassert_ok(wl_set_sink(wl_endpoint_link(&f->endpoint), sink, f));
  zassert_equal(f->reads, 0U);
}

static wl_tx_handle_t send(struct fixture *f) {
  wl_time_ms_t now_ms;
  wl_tx_handle_t handle;
  zassert_ok(wl_endpoint_now(&f->endpoint, &now_ms));
  zassert_ok(wl_send_reliable(wl_endpoint_link(&f->endpoint), 1U, NULL, 0U,
                              now_ms, &handle));
  return handle;
}

ZTEST(wirelink_endpoint_clock, test_first_send_and_idle_gap) {
  init(&first, 60000U);
  wl_tx_handle_t handle = send(&first); /* No priming step. */
  zassert_equal(first.reads, 1U);
  first.time += 4U;
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  zassert_equal(first.sends, 1U);
  ++first.time;
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  zassert_equal(first.sends, 2U);
  zassert_ok(wl_tx_cancel(wl_endpoint_link(&first.endpoint), handle));
  zassert_ok(wl_tx_take(wl_endpoint_link(&first.endpoint), handle,
                        &(wl_tx_result_t){0}));
  first.time = 900000U;
  handle = send(&first);
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  zassert_equal(first.sends, 3U);
  first.time += 4U;
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  zassert_equal(first.sends, 3U);
  ++first.time;
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  zassert_equal(first.sends, 4U);
  (void)handle;
  wl_endpoint_close(&first.endpoint);
}

ZTEST(wirelink_endpoint_clock, test_policy_composes_without_consuming_events) {
  init(&first, 100U);
  policy_events = policy_passes = 0U;
  wl_endpoint_policy_t policy = {.user_data = &first,
    .on_event = observe_event, .progress = policy_progress,
    .deadline_hint = policy_deadline};
  zassert_ok(wl_endpoint_set_policy(&first.endpoint, &policy));
  memset(&policy, 0, sizeof(policy));
  (void)send(&first);
  first.time += 5U;
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  first.time += 5U;
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  zassert_equal(first.events, 1U);
  zassert_equal(policy_events, 1U);
  zassert_equal(policy_passes, 2U);
  wl_poll_hint_t hint;
  zassert_ok(wl_endpoint_get_hint(&first.endpoint, &hint));
  zassert_equal(hint.next_deadline_ms, 3U);
  zassert_ok(wl_endpoint_set_policy(&first.endpoint, NULL));
  zassert_ok(wl_endpoint_get_hint(&first.endpoint, &hint));
  zassert_equal(hint.next_deadline_ms, UINT32_MAX);
  wl_endpoint_close(&first.endpoint);
  zassert_equal(wl_endpoint_set_policy(&first.endpoint, &policy), WL_ERR_NOT_INITIALIZED);
}

ZTEST(wirelink_endpoint_clock, test_completion_service_uses_current_pass) {
  init(&first, 5U);
  first.async = 1;
  (void)send(&first);
  first.time = 70000U;
  first.finish = 1;
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  zassert_equal(first.sends, 1U);
  wl_poll_hint_t hint;
  zassert_ok(wl_endpoint_get_hint(&first.endpoint, &hint));
  zassert_equal(hint.next_deadline_ms, 5U);
  zassert_equal(first.reads, 3U); /* submit, pass, query */
  wl_endpoint_close(&first.endpoint);
}

ZTEST(wirelink_endpoint_clock, test_snapshot_read_budget_and_lifetime) {
  init(&first, 100U);
  (void)send(&first);
  first.time += 5U;
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  first.time += 5U;
  const unsigned reads = first.reads;
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  zassert_equal(first.reads, reads + 1U);
  zassert_equal(first.events, 1U);
  zassert_equal(wl_endpoint_step(&first.endpoint, 0U), WL_ERR_INVALID_ARG);
  zassert_equal(first.reads, reads + 1U);
  wl_endpoint_close(&first.endpoint);
  zassert_equal(wl_endpoint_now(&first.endpoint, &(wl_time_ms_t){0}),
                WL_ERR_NOT_INITIALIZED);
  zassert_equal(first.reads, reads + 1U);
  init(&first, 123U);
  zassert_ok(wl_endpoint_step(&first.endpoint, 1U));
  zassert_equal(first.reads, 1U);
  wl_endpoint_close(&first.endpoint);
}

ZTEST(wirelink_endpoint_clock, test_independent_clocks_and_wrap) {
  init(&first, UINT32_MAX - 2U);
  init(&second, 200U);
  (void)send(&first);
  (void)send(&second);
  first.time = 1U;
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  zassert_equal(first.sends, 1U);
  first.time = 2U;
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  zassert_equal(first.sends, 2U);
  zassert_ok(wl_endpoint_step(&second.endpoint, 8U));
  zassert_equal(second.sends, 1U);
  wl_endpoint_close(&first.endpoint);
  wl_endpoint_close(&second.endpoint);
}

ZTEST(wirelink_endpoint_clock, test_commit_uses_explicit_time) {
  init(&first, 45678U);
  wl_ctx_t *link = wl_endpoint_link(&first.endpoint);
  wl_tx_payload_claim_t claim;
  wl_tx_handle_t handle;
  zassert_ok(wl_tx_payload_claim(link, 1U, WL_DELIVERY_RELIABLE, &claim));
  zassert_ok(wl_tx_payload_commit(link, &claim, 0U, first.time, &handle));
  zassert_ok(wl_endpoint_step(&first.endpoint, 8U));
  zassert_equal(first.sends, 1U);
  zassert_ok(wl_tx_cancel(link, handle));
  zassert_ok(wl_tx_take(link, handle, &(wl_tx_result_t){0}));
  const unsigned reads = first.reads;
  zassert_ok(wl_send_unreliable(link, 1U, NULL, 0U));
  zassert_equal(first.reads, reads);
  wl_endpoint_close(&first.endpoint);
}

ZTEST_SUITE(wirelink_endpoint_clock, NULL, NULL, NULL, NULL, NULL);
