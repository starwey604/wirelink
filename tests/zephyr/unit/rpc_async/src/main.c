/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/rpc_async.h"
#include <stdio.h>
#include <string.h>
#ifdef WL_ASYNC_ZTEST
#include <zephyr/ztest.h>
#endif

#define CHECK(x) do { if (!(x)) { printf("%d: %s\n", __LINE__, #x); return 1; } } while (0)
static wl_ctx_t link;
static wl_rpc_client_t client;
static wl_rpc_async_t async;
static wl_rpc_client_slot_t client_slots[4];
static wl_rpc_async_slot_t slots[4];
static uint8_t requests[4][8], responses[4][8];
static uint32_t transmitted[2], sends, next_tx, cancels, callbacks;
static uint32_t saved_value, followup;
static wl_rpc_client_state_t saved_state;
static int32_t saved_link_error;
static int busy, send_error, encode_error, chain_result, callback_close, callback_init, callback_service;
static uint64_t incarnation;
static wl_time_ms_t now;
static uint16_t configured_count;

/* Link mocks deliberately keep TX ownership after RPC completion/cancel.
 * Real transport/DMA ownership is covered by generated endpoint integration. */
wl_err_t wl_send_unreliable(wl_ctx_t *ctx, uint16_t id, const uint8_t *bytes, size_t size) {
  (void)ctx; (void)id;
  if (send_error) return send_error;
  if (busy) return WL_ERR_BUSY;
  if (size != sizeof(transmitted)) return WL_ERR_INVALID_ARG;
  memcpy(transmitted, bytes, size);
  ++sends;
  return WL_OK;
}
wl_err_t wl_send_reliable(wl_ctx_t *ctx, uint16_t id, const uint8_t *bytes,
    size_t size, wl_time_ms_t time, wl_tx_handle_t *handle) {
  (void)time;
  wl_err_t error = wl_send_unreliable(ctx, id, bytes, size);
  if (error == WL_OK) { busy = 1; *handle = ++next_tx; }
  return error;
}
wl_err_t wl_tx_cancel(wl_ctx_t *ctx, wl_tx_handle_t handle) {
  (void)ctx; (void)handle; ++cancels; return WL_OK;
}
wl_err_t wl_tx_status(const wl_ctx_t *ctx, wl_tx_handle_t handle, wl_tx_state_t *out) {
  (void)ctx;
  if (!busy || handle == 0U || handle != next_tx) return WL_ERR_NOT_FOUND;
  *out = WL_TX_STATE_WAITING_ACK;
  return WL_OK;
}

static wl_err_t encode(const void *request, uint32_t id, uint8_t *out,
    size_t capacity, size_t *length) {
  if (encode_error) return encode_error;
  if (capacity < 8U) return WL_ERR_BUF_TOO_SMALL;
  memcpy(out, &id, sizeof(id));
  memcpy(out + 4U, request, 4U);
  *length = 8U;
  return WL_OK;
}
static void prepare(void *context, const wl_rpc_client_result_t *result) {
  (void)context;
  saved_state = result->state;
  saved_link_error = result->link_result;
  saved_value = 0U;
  if (result->state == WL_RPC_CLIENT_COMPLETED && result->response_length == 4U)
    memcpy(&saved_value, result->response_data, 4U);
}
static void notify(void *context, wl_rpc_callback_t callback, void *user_data) {
  (void)context;
  ((void (*)(void *))callback)(user_data);
}
static void completed(void *user_data);
static const wl_rpc_async_observer_t observer = {
    prepare, notify, NULL, (wl_rpc_callback_t)completed, NULL};

static wl_err_t submit(uint32_t value, uint32_t timeout, wl_delivery_t delivery,
    wl_rpc_call_t *call) {
  return wl_rpc_async_submit(&async, 2U, 3U, delivery, timeout, now,
      encode, &value, &observer, call);
}
static void completed(void *user_data) {
  (void)user_data;
  ++callbacks;
  uint16_t notified;
  callback_service = wl_rpc_async_service(&async, now, &notified);
  callback_close = wl_rpc_async_close(&async);
  callback_init = wl_rpc_async_init(&async, &link, &client, slots,
      configured_count, requests[0], sizeof(requests), 8U, incarnation + 1U);
  if (followup) {
    --followup;
    chain_result = submit(99U, 100U, WL_DELIVERY_UNRELIABLE, NULL);
  }
}
static int init(uint16_t count) {
  CHECK(wl_rpc_async_close(&async) == WL_OK);
  const wl_rpc_client_config_t config = {
      client_slots, count, responses[0], sizeof(responses), 8U, 1U};
  CHECK(wl_rpc_client_init(&client, &config) == WL_RPC_OK);
  configured_count = count;
  CHECK(wl_rpc_async_init(&async, &link, &client, slots, count,
      requests[0], sizeof(requests), 8U, ++incarnation) == WL_OK);
  busy = send_error = encode_error = chain_result = 0;
  callbacks = sends = cancels = next_tx = saved_value = followup = 0U;
  saved_state = WL_RPC_CLIENT_FREE;
  now = 100U;
  return 0;
}
static int service(void) {
  uint16_t expired, notified;
  CHECK(wl_rpc_client_poll(&client, now, &expired) == WL_RPC_OK);
  CHECK(wl_rpc_async_service(&async, now, &notified) == WL_OK);
  CHECK(notified <= configured_count);
  return 0;
}
static int respond(uint32_t id, uint32_t value) {
  CHECK(wl_rpc_client_on_response(&client, 3U, id, 0,
      (const uint8_t *)&value, sizeof(value)) == WL_RPC_OK);
  return 0;
}

static int snapshot_queue_and_deadline(void) {
  wl_rpc_call_t first, second, unchanged;
  CHECK(init(2U) == 0);
  busy = 1;
  uint32_t value = 17U;
  CHECK(wl_rpc_async_submit(&async, 2U, 3U, WL_DELIVERY_UNRELIABLE,
      10U, now, encode, &value, &observer, &first) == WL_OK);
  value = 99U;
  CHECK(requests[0][4] == 17U); /* Accepted stack value was snapshotted. */
  CHECK(submit(20U, 20U, WL_DELIVERY_UNRELIABLE, &second) == WL_OK);
  unchanged = first;
  CHECK(submit(21U, 30U, WL_DELIVERY_UNRELIABLE, &first) == WL_ERR_BUSY);
  CHECK(memcmp(&first, &unchanged, sizeof(first)) == 0 && callbacks == 0U);
  now += 10U;
  CHECK(service() == 0);
  CHECK(callbacks == 1U && saved_state == WL_RPC_CLIENT_TIMED_OUT && sends == 0U);
  busy = 0;
  CHECK(service() == 0);
  CHECK(sends == 1U && transmitted[1] == 20U);
  now += 10U;
  CHECK(service() == 0);
  CHECK(callbacks == 2U && saved_state == WL_RPC_CLIENT_TIMED_OUT);
  CHECK(wl_rpc_async_cancel(&async, &first) == WL_ERR_NOT_FOUND);
  CHECK(wl_rpc_async_cancel(&async, &second) == WL_ERR_NOT_FOUND);

  /* A new API call after idle must not transmit expired queued work. */
  CHECK(init(2U) == 0);
  busy = 1;
  now = UINT32_MAX - 3U;
  CHECK(submit(31U, 8U, WL_DELIVERY_UNRELIABLE, NULL) == WL_OK);
  now += 8U;
  busy = 0;
  CHECK(submit(32U, 20U, WL_DELIVERY_UNRELIABLE, NULL) == WL_OK);
  CHECK(sends == 1U && transmitted[1] == 32U && callbacks == 0U);
  CHECK(wl_rpc_async_notification_deadline(&async) == 0U);
  CHECK(service() == 0);
  CHECK(callbacks == 1U && saved_state == WL_RPC_CLIENT_TIMED_OUT);
  return 0;
}

static int callback_chain_and_detached_tx(void) {
  wl_rpc_call_t first, second;
  CHECK(init(1U) == 0);
  CHECK(submit(42U, 100U, WL_DELIVERY_RELIABLE, &first) == WL_OK);
  CHECK(sends == 1U && busy == 1);
  CHECK(respond(transmitted[0], 43U) == 0); /* Application reply before request ACK. */
  followup = 1U;
  CHECK(service() == 0);
  CHECK(callbacks == 1U && saved_value == 43U && chain_result == WL_OK);
  CHECK(callback_close == WL_ERR_REENTRANT && callback_init == WL_ERR_INVALID_STATE);
  CHECK(callback_service == WL_ERR_REENTRANT);
  CHECK(busy == 1 && cancels == 0U && sends == 1U);
  CHECK(wl_rpc_async_cancel(&async, &first) == WL_ERR_NOT_FOUND);
  /* The original link transaction must be drained independently. */
  const wl_event_t terminal = {.type = WL_EVT_TX_SUCCESS, .handle = next_tx};
  CHECK(wl_rpc_client_on_tx_event(&client, &terminal) == WL_RPC_ERR_NOT_FOUND);
  CHECK(wl_rpc_async_retire_tx(&async, terminal.handle) == 1U);
  CHECK(wl_rpc_async_retire_tx(&async, terminal.handle) == 0U);
  busy = 0; /* Mock owner has now taken that terminal transaction. */
  CHECK(service() == 0);
  CHECK(sends == 2U && transmitted[1] == 99U && saved_value == 43U);
  CHECK(respond(transmitted[0], 100U) == 0);
  CHECK(service() == 0 && callbacks == 2U && saved_value == 100U);
  CHECK(submit(44U, 100U, WL_DELIVERY_UNRELIABLE, &second) == WL_OK);
  CHECK(wl_rpc_async_cancel(&async, &second) == WL_OK);
  CHECK(wl_rpc_client_on_response(&client, 3U, transmitted[0], 0, NULL, 0U) == WL_RPC_ERR_INVALID_STATE);
  CHECK(service() == 0 && callbacks == 3U && saved_state == WL_RPC_CLIENT_CANCELLED);
  return 0;
}

static int failed_admission_and_communication(void) {
  wl_rpc_client_result_t raw = {0};
  wl_rpc_completion_t outcome;
  raw.state = WL_RPC_CLIENT_APPLICATION_ERROR;
  raw.application_status = 17;
  wl_rpc_async_completion(&raw, &outcome);
  CHECK(outcome.status == WL_RPC_REJECTED && outcome.rejection == 17);
  raw.runtime_error = WL_RPC_ERR_RESPONSE_TOO_LARGE;
  wl_rpc_async_completion(&raw, &outcome);
  CHECK(outcome.status == WL_RPC_FAILED && outcome.rejection == 0 &&
      outcome.runtime_error == WL_RPC_ERR_RESPONSE_TOO_LARGE);
  CHECK(init(1U) == 0);
  encode_error = WL_ERR_CORRUPT_PAYLOAD;
  CHECK(submit(0U, 10U, WL_DELIVERY_RELIABLE, NULL) == WL_ERR_CORRUPT_PAYLOAD);
  CHECK(callbacks == 0U && sends == 0U);
  encode_error = 0;
  CHECK(submit(0U, 0U, WL_DELIVERY_RELIABLE, NULL) == WL_ERR_INVALID_ARG);
  CHECK(submit(0U, UINT32_C(0x80000000), WL_DELIVERY_RELIABLE, NULL) == WL_ERR_INVALID_ARG);
  send_error = WL_ERR_IO;
  CHECK(submit(1U, 100U, WL_DELIVERY_RELIABLE, NULL) == WL_OK);
  CHECK(callbacks == 0U && wl_rpc_async_notification_deadline(&async) == 0U);
  CHECK(service() == 0);
  CHECK(callbacks == 1U && saved_state == WL_RPC_CLIENT_LINK_FAILED && saved_link_error == WL_ERR_IO);
  CHECK(submit(2U, 100U, WL_DELIVERY_RELIABLE, NULL) == WL_OK);
  CHECK(service() == 0 && callbacks == 2U);
  return 0;
}

static int cancel_completion_is_local(void) {
  wl_rpc_call_t first, second;
  CHECK(init(2U) == 0);
  CHECK(submit(71U, 100U, WL_DELIVERY_RELIABLE, &first) == WL_OK);
  CHECK(submit(72U, 100U, WL_DELIVERY_RELIABLE, &second) == WL_OK);
  CHECK(wl_rpc_async_cancel_complete(&async, &second) == WL_OK);
  CHECK(callbacks == 1 && saved_state == WL_RPC_CLIENT_CANCELLED);
  CHECK(sends == 1 && cancels == 0 && busy == 1); /* First call/TX is untouched. */
  CHECK(wl_rpc_async_cancel_complete(&async, &second) == WL_ERR_NOT_FOUND);
  CHECK(service() == 0 && callbacks == 1);
  CHECK(wl_rpc_async_cancel_complete(&async, &first) == WL_OK);
  CHECK(callbacks == 2 && cancels == 1 && busy == 1);
  CHECK(wl_rpc_async_retire_tx(&async, next_tx) == 1);
  CHECK(callback_service == WL_ERR_REENTRANT && callback_close == WL_ERR_REENTRANT);
  CHECK(service() == 0 && callbacks == 2);
  return 0;
}

static int close_and_stale_handles(void) {
  wl_rpc_call_t first, second;
  CHECK(init(2U) == 0);
  CHECK(submit(51U, 100U, WL_DELIVERY_UNRELIABLE, &first) == WL_OK);
  const uint32_t first_id = transmitted[0];
  CHECK(submit(52U, 100U, WL_DELIVERY_UNRELIABLE, &second) == WL_OK);
  CHECK(respond(transmitted[0], 53U) == 0); /* Second completes first. */
  CHECK(wl_rpc_async_cancel(&async, &second) == WL_ERR_INVALID_STATE);
  followup = 1U;
  CHECK(wl_rpc_async_close(&async) == WL_OK);
  CHECK(callbacks == 2U && saved_state == WL_RPC_CLIENT_COMPLETED && saved_value == 53U);
  CHECK(chain_result == WL_ERR_NOT_INITIALIZED);
  CHECK(callback_close == WL_ERR_REENTRANT && callback_init == WL_ERR_INVALID_STATE);
  CHECK(wl_rpc_async_close(&async) == WL_OK && callbacks == 2U);
  CHECK(wl_rpc_client_on_response(&client, 3U, first_id, 0, NULL, 0U) == WL_RPC_ERR_NOT_FOUND);
  CHECK(init(1U) == 0);
  CHECK(submit(61U, 100U, WL_DELIVERY_UNRELIABLE, NULL) == WL_OK);
  CHECK(wl_rpc_async_cancel(&async, &first) == WL_ERR_NOT_FOUND);
  CHECK(wl_rpc_async_cancel(&async, &second) == WL_ERR_NOT_FOUND);
  CHECK(wl_rpc_async_close(&async) == WL_OK && callbacks == 1U);
  return 0;
}

#ifdef WL_ASYNC_ZTEST
ZTEST(wirelink_rpc_async, test_snapshot_queue_and_deadline) { zassert_equal(snapshot_queue_and_deadline(), 0); }
ZTEST(wirelink_rpc_async, test_callback_chain_and_detached_tx) { zassert_equal(callback_chain_and_detached_tx(), 0); }
ZTEST(wirelink_rpc_async, test_failed_admission_and_communication) { zassert_equal(failed_admission_and_communication(), 0); }
ZTEST(wirelink_rpc_async, test_close_and_stale_handles) { zassert_equal(close_and_stale_handles(), 0); }
ZTEST(wirelink_rpc_async, test_cancel_completion_is_local) { zassert_equal(cancel_completion_is_local(), 0); }
ZTEST_SUITE(wirelink_rpc_async, NULL, NULL, NULL, NULL, NULL);
#else
int main(void) {
  CHECK(snapshot_queue_and_deadline() == 0);
  CHECK(callback_chain_and_detached_tx() == 0);
  CHECK(failed_admission_and_communication() == 0);
  CHECK(close_and_stale_handles() == 0);
  CHECK(cancel_completion_is_local() == 0);
  puts("RPC async ownership/queue contracts: PASS");
  return 0;
}
#endif
