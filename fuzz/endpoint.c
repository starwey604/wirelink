/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/endpoint.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
#define INPUT_MAX 4096U
typedef struct { wl_call_handle_t handle; unsigned callbacks; uint32_t copied; } job_t;
typedef struct {
  wl_endpoint_t core; wl_packet_port_t port;
  uint8_t rx[2][256], tx[WL_ENDPOINT_TX_COUNT][64], captured[256];
  const uint8_t *borrowed; size_t length;
  wl_packet_token_t token;
  wl_packet_submit_t mode;
  bool physical;
  job_t jobs[INPUT_MAX]; unsigned admitted;
  uint32_t now;
} fixture_t;
static fixture_t fixture;
static uint32_t identity = 1000;
static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  fixture_t *f = user; CHECK(!f->physical && length <= sizeof(f->captured));
  if (f->mode == WL_PACKET_BUSY || f->mode == WL_PACKET_FAILED) return f->mode;
  memcpy(f->captured, data, length); f->length = length;
  if (f->mode == WL_PACKET_STARTED) {
    f->borrowed = data; f->token = token; f->physical = true;
  }
  return f->mode;
}
static void complete(wl_endpoint_t *core, const wl_endpoint_call_t *call, wl_rpc_result_t result,
                      const uint8_t *body, size_t length) {
  (void)core; job_t *job = call->user; CHECK(++job->callbacks == 1);
  /* Emulate a fixed-width codec's borrowed-result conversion, without retaining
   * body or adding any operation-matching/deadline table outside the kernel. */
  if (result.outcome == WL_RPC_SUCCESS && length == 4) memcpy(&job->copied, body, 4);
}
static void callback(void) {}
static wl_err_t route(wl_endpoint_t *core, const wl_wire_frame_t *frame) {
  if (frame->message_id == 22) return wl_endpoint_response(core, frame);
  if (frame->message_id != 21) return WL_OK;
  if (frame->kind != WL_WIRE_RPC_REQ) return WL_ERR_CORRUPT_PAYLOAD;
  wl_rpc_metadata_t metadata;
  wl_err_t error = wl_wire_rpc_read(frame->kind, frame->payload, frame->length, &metadata);
  if (error != WL_OK) return error;
  uint8_t slot;
  uint8_t *payload;
  size_t capacity = 0U;
  error = wl_endpoint_reply_claim(core, 22, &metadata, &slot, &payload, &capacity);
  if (error != WL_OK) return error;
  if (metadata.length != 4) {
    wl_endpoint_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
  }
  uint32_t value; memcpy(&value, metadata.body, 4);
  if (value == 7) return wl_endpoint_reply_error(core, slot, 7);
  if (capacity < 4) {
    wl_endpoint_reply_abort(core, slot); return WL_ERR_BUF_TOO_SMALL;
  }
  memcpy(payload, &value, 4);
  return wl_endpoint_reply_commit(core, slot, 4);
}
static void publish(fixture_t *f, unsigned flags) {
  if (!f->physical) return;
  if (f->borrowed) CHECK(memcmp(f->borrowed, f->captured, f->length) == 0);
  CHECK(wl_packet_tx_publish(&f->port, f->token, flags) == WL_OK);
  if (flags & (WL_PACKET_CONSUMED | WL_PACKET_DRAINED | WL_PACKET_TX_ERROR)) f->borrowed = NULL;
  if (flags & (WL_PACKET_DRAINED | WL_PACKET_TX_ERROR)) f->physical = false;
}
static void close_fixture(fixture_t *f) {
  CHECK(wl_endpoint_close(&f->core) == WL_OK);
  if (f->physical) {
    /* Actual model cancellation: abandon source ownership before publishing. */
    if (f->borrowed) CHECK(memcmp(f->borrowed, f->captured, f->length) == 0);
    f->borrowed = NULL; f->physical = false;
    CHECK(wl_packet_tx_publish(&f->port, f->token, WL_PACKET_TX_ERROR) == WL_OK);
  }
  for (unsigned i = 0; i < 4 && !wl_endpoint_quiescent(&f->core); ++i)
    (void)wl_endpoint_step(&f->core, f->now, 16);
  CHECK(wl_endpoint_quiescent(&f->core));
  for (unsigned i = 0; i < f->admitted; ++i) CHECK(f->jobs[i].callbacks == 1);
}
static void init(fixture_t *f) {
  CHECK(identity != UINT32_MAX);
  wl_packet_config_t packet = {.instance = ++identity, .rx_storage = &f->rx[0][0],
    .rx_unit_size = sizeof(f->rx[0]), .rx_storage_size = sizeof(f->rx), .submit = sink, .user = f};
  CHECK(wl_packet_init(&f->port, &packet) == WL_OK);
  wl_endpoint_config_t config = {.port = &f->port,
    .storage = wl_endpoint_storage_uniform(&f->tx[0][0], sizeof(f->tx[0])),
    .route = route};
  CHECK(wl_endpoint_init(&f->core, &config) == WL_OK);
}
static void inject(fixture_t *f, const uint8_t *data, size_t length) {
  wl_packet_claim_t claim;
  if (wl_packet_rx_claim(&f->port, length, &claim) == WL_OK) {
    memcpy(claim.span.data, data, length);
    CHECK(wl_packet_rx_commit(&f->port, &claim, length) == WL_OK);
  }
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > INPUT_MAX) return 0;
  fixture_t *f = &fixture; memset(f, 0, sizeof(*f)); f->mode = WL_PACKET_STARTED;
  f->now = size && (data[0] & 128) ? UINT32_MAX - 128U : 0U;
  init(f);
  for (size_t at = 0; at < size; ++at) {
    uint8_t action = data[at];
    switch (action % 12) {
      case 0: {
        CHECK(f->admitted < INPUT_MAX); job_t *job = &f->jobs[f->admitted];
        uint32_t value = action;
        uint8_t slot;
        uint8_t *payload;
        size_t capacity = 0U;
        wl_err_t error = wl_endpoint_call_claim(&f->core, &slot, &payload, &capacity);
        if (error == WL_OK) {
          CHECK(capacity >= 4U);
          memcpy(payload, &value, 4U);
          error = wl_endpoint_call_commit(&f->core, slot, 21U, 22U, 4U, f->now,
              1U + action, 1U, complete, callback, job, &job->handle);
        }
        value = 0; CHECK(job->callbacks == 0);
        if (error == WL_OK) ++f->admitted;
        break;
      }
      case 1:
        (void)wl_endpoint_step(&f->core, f->now, 1 + action % 16); break;
      case 2:
        if (f->admitted) (void)wl_endpoint_cancel(&f->core, f->jobs[action % f->admitted].handle);
        break;
      case 3:
        f->now += action + 1; break;
      case 4:
        if (f->physical && f->borrowed) publish(f, WL_PACKET_CONSUMED);
        break;
      case 5: publish(f, WL_PACKET_DRAINED); break;
      case 6: publish(f, WL_PACKET_TX_ERROR); break;
      case 7:
        f->mode = (wl_packet_submit_t)(action / 12 % 4); wl_packet_activity(&f->port); break;
      case 8:
        close_fixture(f); init(f); break;
      case 9: {
        size_t length = size - at; if (length > 64) length = 64;
        inject(f, data + at, length); at += length - 1; break;
      }
      case 10:
        if (f->length) inject(f, f->captured, f->length);
        break;
      default: {
        uint32_t value = action;
        uint8_t *frame;
        size_t capacity = 0U;
        if (wl_endpoint_msg_claim(&f->core, &frame, &capacity) == WL_OK &&
            capacity >= WL_WIRE_NATIVE_HEADER + 4U) {
          memcpy(frame + WL_WIRE_NATIVE_HEADER, &value, 4U);
          if (wl_wire_frame_seal(frame, capacity, WL_WIRE_MSG, 23U, 4U) == WL_OK)
            (void)wl_endpoint_msg_commit(&f->core, WL_WIRE_NATIVE_HEADER + 4U);
        }
        value = 0;
        break;
      }
    }
  }
  close_fixture(f); return 0;
}
