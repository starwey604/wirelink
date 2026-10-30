/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/kernel.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
#define INPUT_MAX 4096U
typedef struct { wl_kernel_call_handle_t handle; unsigned callbacks; uint32_t copied; } job_t;
typedef struct {
  wl_kernel_t core; wl_packet_port_t port;
  uint8_t rx[2][256], tx[9][64], captured[256];
  const uint8_t *borrowed; size_t length;
  wl_packet_token_t token;
  wl_packet_submit_t mode;
  bool physical;
  job_t jobs[INPUT_MAX]; unsigned admitted;
  uint32_t now;
} fixture_t;
static fixture_t fixture;
static uint64_t identity = 1000;
static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  fixture_t *f = user; CHECK(!f->physical && length <= sizeof(f->captured));
  if (f->mode == WL_PACKET_BUSY || f->mode == WL_PACKET_FAILED) return f->mode;
  memcpy(f->captured, data, length); f->length = length;
  if (f->mode == WL_PACKET_STARTED) {
    f->borrowed = data; f->token = token; f->physical = true;
  }
  return f->mode;
}
static wl_err_t encode(const void *user, uint8_t *data, size_t capacity, size_t *length) {
  if (capacity < 4) return WL_ERR_BUF_TOO_SMALL;
  memcpy(data, user, 4); *length = 4; return WL_OK;
}
static void complete(wl_kernel_t *core, const wl_kernel_call_t *call, wl_kernel_result_t result,
                      const uint8_t *body, size_t length) {
  (void)core; job_t *job = call->user; CHECK(++job->callbacks == 1);
  /* Emulate a fixed-width codec's borrowed-result conversion, without retaining
   * body or adding any operation-matching/deadline table outside the kernel. */
  if (result.outcome == WL_KERNEL_SUCCESS && length == 4) memcpy(&job->copied, body, 4);
}
static void callback(void) {}
static wl_err_t route(wl_kernel_t *core, const wl_kernel_frame_t *frame) {
  if (frame->message_id == 22) return wl_kernel_response(core, 22, frame->payload, frame->length);
  if (frame->message_id != 21) return WL_OK;
  unsigned slot;
  wl_err_t error = wl_kernel_reply_reserve(core, &slot);
  if (error != WL_OK) return error;
  wl_kernel_rpc_metadata_t metadata;
  error = wl_kernel_rpc_read(frame->payload, frame->length, 1, &metadata);
  if (error != WL_OK || metadata.length != 4) {
    wl_kernel_reply_abort(core, slot); return WL_ERR_CORRUPT_PAYLOAD;
  }
  uint32_t value; memcpy(&value, metadata.body, 4);
  return wl_kernel_reply_finish(core, slot, 22, &metadata, value == 7 ? 7 : 0, &value, encode);
}
static void publish(fixture_t *f, unsigned flags) {
  if (!f->physical) return;
  if (f->borrowed) CHECK(memcmp(f->borrowed, f->captured, f->length) == 0);
  CHECK(wl_packet_tx_publish(&f->port, f->token, flags) == WL_OK);
  if (flags & (WL_PACKET_CONSUMED | WL_PACKET_DRAINED | WL_PACKET_TX_ERROR)) f->borrowed = NULL;
  if (flags & (WL_PACKET_DRAINED | WL_PACKET_TX_ERROR)) f->physical = false;
}
static void close_fixture(fixture_t *f) {
  CHECK(wl_kernel_close(&f->core) == WL_OK);
  if (f->physical) {
    /* Actual model cancellation: abandon source ownership before publishing. */
    if (f->borrowed) CHECK(memcmp(f->borrowed, f->captured, f->length) == 0);
    f->borrowed = NULL; f->physical = false;
    CHECK(wl_packet_tx_publish(&f->port, f->token, WL_PACKET_TX_ERROR) == WL_OK);
  }
  for (unsigned i = 0; i < 4 && !wl_kernel_quiescent(&f->core); ++i)
    (void)wl_kernel_step(&f->core, f->now, 16);
  CHECK(wl_kernel_quiescent(&f->core));
  for (unsigned i = 0; i < f->admitted; ++i) CHECK(f->jobs[i].callbacks == 1);
}
static void init(fixture_t *f) {
  CHECK(identity != UINT64_MAX);
  wl_packet_config_t packet = {.instance = ++identity, .rx_storage = &f->rx[0][0],
    .rx_unit_size = sizeof(f->rx[0]), .rx_storage_size = sizeof(f->rx), .submit = sink, .user = f};
  CHECK(wl_packet_init(&f->port, &packet) == WL_OK);
  wl_kernel_config_t config = {.port = &f->port, .route = route};
  for (unsigned i = 0; i < 9; ++i) config.tx_storage[i] = (wl_span_t){f->tx[i], sizeof(f->tx[i])};
  CHECK(wl_kernel_init(&f->core, &config) == WL_OK);
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
        wl_err_t error = wl_kernel_call_start(&f->core, 21, 22, &value, encode,
          f->now, 1 + action, complete, callback, job, &job->handle);
        value = 0; CHECK(job->callbacks == 0);
        if (error == WL_OK) ++f->admitted;
        break;
      }
      case 1:
        (void)wl_kernel_step(&f->core, f->now, 1 + action % 16); break;
      case 2:
        if (f->admitted) (void)wl_kernel_cancel(&f->core, f->jobs[action % f->admitted].handle);
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
        (void)wl_kernel_send(&f->core, 23, &value, encode); value = 0;
        break;
      }
    }
  }
  close_fixture(f); return 0;
}
