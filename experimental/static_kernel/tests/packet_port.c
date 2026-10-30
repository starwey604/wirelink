/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WL_PACKET_ZTEST
#include <zephyr/ztest.h>
#define CHECK(x) zassert_true((x), "%s:%d: %s", __FILE__, __LINE__, #x)
#else
#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)
#endif

typedef struct {
  wl_packet_port_t port;
  uint8_t storage[2][32];
  wl_packet_submit_t result;
  unsigned notifications;
  unsigned submissions;
  unsigned inline_flags;
  const uint8_t *source;
  wl_packet_token_t token;
} fixture_t;

static void notify(void *user) { ++((fixture_t *)user)->notifications; }

static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t length,
                               wl_packet_token_t token) {
  fixture_t *f = user;
  CHECK(length > 0U);
  f->source = data;
  f->token = token;
  ++f->submissions;
  if (f->inline_flags != 0U)
    CHECK(wl_packet_tx_publish(&f->port, token, f->inline_flags) == WL_OK);
  return f->result;
}

static void init(fixture_t *f, uint64_t instance) {
  wl_packet_config_t config = {
      .instance = instance, .rx_storage = &f->storage[0][0],
      .rx_unit_size = sizeof(f->storage[0]), .rx_storage_size = sizeof(f->storage),
      .submit = sink, .notify = notify, .user = f
  };
  CHECK(wl_packet_init(&f->port, &config) == WL_OK);
}

static void test_rx_ownership(void) {
  fixture_t f = {0};
  wl_packet_claim_t a, b, invalid;
  wl_packet_borrow_t borrow, stale;
  init(&f, 101U);
  CHECK(wl_packet_rx_claim(&f.port, 8U, &a) == WL_OK);
  invalid = a;
  invalid.span.length = 32U;
  CHECK(wl_packet_rx_commit(&f.port, &invalid, 9U) == WL_ERR_INVALID_ARG);
  CHECK(wl_packet_rx_claim(&f.port, 8U, &b) == WL_ERR_BUSY);
  memset(a.span.data, 0xa1, 8U);
  CHECK(wl_packet_rx_commit(&f.port, &a, 8U) == WL_OK);
  CHECK(wl_packet_rx_commit(&f.port, &a, 8U) == WL_ERR_NOT_FOUND);
  CHECK(wl_packet_rx_claim(&f.port, 32U, &b) == WL_OK);
  memset(b.span.data, 0xb2, 32U);
  CHECK(wl_packet_rx_commit(&f.port, &b, 32U) == WL_OK);
  CHECK(wl_packet_rx_claim(&f.port, 8U, &invalid) == WL_ERR_WOULD_BLOCK);
  CHECK(wl_packet_rx_borrow(&f.port, &borrow) == WL_OK);
  CHECK(borrow.length == 8U && borrow.data[7] == 0xa1);
  stale = borrow;
  CHECK(wl_packet_rx_borrow(&f.port, &borrow) == WL_ERR_BUSY);
  CHECK(wl_packet_rx_release(&f.port, &borrow) == WL_OK);
  CHECK(wl_packet_rx_release(&f.port, &stale) == WL_ERR_NOT_FOUND);
  CHECK(wl_packet_rx_borrow(&f.port, &borrow) == WL_OK);
  CHECK(borrow.length == 32U && borrow.data[31] == 0xb2);
  CHECK(wl_packet_rx_release(&f.port, &borrow) == WL_OK);
  CHECK(!wl_packet_rx_ready(&f.port));
  CHECK(wl_packet_rx_claim(&f.port, 8U, &a) == WL_OK);
  CHECK(wl_packet_rx_abort(&f.port, &a) == WL_OK);
  CHECK(wl_packet_rx_claim(&f.port, 8U, &b) == WL_OK);
  CHECK(a.span.data == b.span.data);
  CHECK(wl_packet_rx_abort(&f.port, &a) == WL_ERR_NOT_FOUND);
  CHECK(wl_packet_rx_abort(&f.port, &b) == WL_OK);
  CHECK(wl_packet_take_activity(&f.port));
  CHECK(!wl_packet_take_activity(&f.port));
  wl_packet_activity(&f.port);
  CHECK(wl_packet_take_activity(&f.port));
}

static void test_tx_lease(void) {
  fixture_t f = {.result = WL_PACKET_BUSY};
  const uint8_t source[] = {1U, 2U, 3U};
  wl_packet_token_t token, event, stale;
  init(&f, 102U);
  CHECK(wl_packet_tx_submit(&f.port, source, sizeof(source), &token) == WL_ERR_WOULD_BLOCK);
  CHECK(!wl_packet_tx_active(&f.port));
  f.result = WL_PACKET_FAILED;
  CHECK(wl_packet_tx_submit(&f.port, source, sizeof(source), &token) == WL_ERR_IO);
  CHECK(!wl_packet_tx_active(&f.port));
  f.result = WL_PACKET_STARTED;
  CHECK(wl_packet_tx_submit(&f.port, source, sizeof(source), &token) == WL_OK);
  CHECK(wl_packet_tx_active(&f.port) && f.source == source);
  CHECK(wl_packet_tx_submit(&f.port, source, sizeof(source), &stale) == WL_ERR_WOULD_BLOCK);
  CHECK(wl_packet_tx_take(&f.port, &event) == 0U);
  CHECK(wl_packet_tx_publish(&f.port, token, WL_PACKET_CONSUMED) == WL_OK);
  CHECK(wl_packet_tx_publish(&f.port, token, WL_PACKET_CONSUMED) == WL_ERR_NOT_FOUND);
  CHECK(wl_packet_tx_take(&f.port, &event) == WL_PACKET_CONSUMED);
  CHECK(event.sequence == token.sequence && event.instance == token.instance);
  CHECK(wl_packet_tx_take(&f.port, &event) == 0U);
  CHECK(wl_packet_tx_active(&f.port));
  CHECK(wl_packet_tx_publish(&f.port, token, WL_PACKET_DRAINED) == WL_OK);
  CHECK(wl_packet_tx_take(&f.port, &event) == WL_PACKET_DRAINED);
  CHECK(!wl_packet_tx_active(&f.port));
  stale = token;
  CHECK(wl_packet_tx_submit(&f.port, source, sizeof(source), &token) == WL_OK);
  CHECK(wl_packet_tx_publish(&f.port, stale, WL_PACKET_DRAINED) == WL_ERR_NOT_FOUND);
  CHECK(wl_packet_tx_take(&f.port, &event) == 0U);
  CHECK(wl_packet_tx_publish(&f.port, token, WL_PACKET_TX_ERROR) == WL_OK);
  CHECK(wl_packet_tx_take(&f.port, &event) ==
        (WL_PACKET_TX_ERROR | WL_PACKET_CONSUMED | WL_PACKET_DRAINED));
  f.inline_flags = WL_PACKET_DRAINED;
  CHECK(wl_packet_tx_submit(&f.port, source, sizeof(source), &token) == WL_OK);
  CHECK(wl_packet_tx_take(&f.port, &event) == (WL_PACKET_CONSUMED | WL_PACKET_DRAINED));
  f.inline_flags = 0U;
  f.result = WL_PACKET_SENT;
  CHECK(wl_packet_tx_submit(&f.port, source, sizeof(source), &token) == WL_OK);
  CHECK(wl_packet_tx_take(&f.port, &event) == (WL_PACKET_CONSUMED | WL_PACKET_DRAINED));
}

static void test_stop_reinit_exhaustion(void) {
  fixture_t f = {.result = WL_PACKET_STARTED};
  uint8_t source = 9U;
  wl_packet_claim_t old_claim, claim;
  wl_packet_token_t old_tx, tx;
  wl_packet_borrow_t borrow;
  init(&f, 103U);
  CHECK(wl_packet_rx_claim(&f.port, 8U, &old_claim) == WL_OK);
  CHECK(wl_packet_tx_submit(&f.port, &source, 1U, &old_tx) == WL_OK);
  wl_packet_stop(&f.port);
  CHECK(!wl_packet_quiescent(&f.port));
  CHECK(wl_packet_rx_commit(&f.port, &old_claim, 1U) == WL_ERR_INVALID_STATE);
  CHECK(wl_packet_rx_claim(&f.port, 8U, &claim) == WL_ERR_INVALID_STATE);
  CHECK(wl_packet_tx_submit(&f.port, &source, 1U, &tx) == WL_ERR_INVALID_STATE);
  CHECK(wl_packet_tx_publish(&f.port, old_tx, WL_PACKET_DRAINED) == WL_OK);
  CHECK(wl_packet_tx_take(&f.port, &tx) != 0U);
  CHECK(wl_packet_quiescent(&f.port));
  init(&f, 104U); /* Fixture adapter has stopped all I/O. */
  CHECK(wl_packet_rx_claim(&f.port, 8U, &claim) == WL_OK);
  CHECK(wl_packet_rx_abort(&f.port, &old_claim) == WL_ERR_NOT_FOUND);
  CHECK(wl_packet_rx_abort(&f.port, &claim) == WL_OK);
  CHECK(wl_packet_tx_submit(&f.port, &source, 1U, &tx) == WL_OK);
  CHECK(wl_packet_tx_publish(&f.port, old_tx, WL_PACKET_DRAINED) == WL_ERR_NOT_FOUND);
  CHECK(wl_packet_tx_publish(&f.port, tx, WL_PACKET_DRAINED) == WL_OK);
  CHECK(wl_packet_tx_take(&f.port, &tx) != 0U);
  /* Deliberately place the independent ring cursors across u32 wrap. */
  atomic_store(&f.port.read_cursor, UINT32_MAX);
  atomic_store(&f.port.write_cursor, UINT32_MAX);
  for (unsigned i = 0; i < 3U; ++i) {
    CHECK(wl_packet_rx_claim(&f.port, 1U, &claim) == WL_OK);
    claim.span.data[0] = (uint8_t)i;
    CHECK(wl_packet_rx_commit(&f.port, &claim, 1U) == WL_OK);
    CHECK(wl_packet_rx_borrow(&f.port, &borrow) == WL_OK);
    CHECK(borrow.data[0] == i);
    CHECK(wl_packet_rx_release(&f.port, &borrow) == WL_OK);
  }
  /* Reuse the saved configuration after quiescence, with a fresh instance.
   * init must snapshot aliased configuration before clearing the port. */
  wl_packet_stop(&f.port);
  CHECK(wl_packet_quiescent(&f.port));
  f.port.config.instance = 105U;
  CHECK(wl_packet_init(&f.port, &f.port.config) == WL_OK);
  CHECK(wl_packet_rx_claim(&f.port, 1U, &claim) == WL_OK);
  CHECK(claim.token.instance == 105U);
  CHECK(wl_packet_rx_abort(&f.port, &claim) == WL_OK);
  f.port.claim_sequence = UINT32_MAX;
  CHECK(wl_packet_rx_claim(&f.port, 1U, &claim) == WL_ERR_ID_EXHAUSTED);
  f.port.tx_sequence = WL_PACKET_SEQUENCE_MAX;
  CHECK(wl_packet_tx_submit(&f.port, &source, 1U, &tx) == WL_ERR_ID_EXHAUSTED);
}

#ifdef WL_PACKET_ZTEST
ZTEST(static_packet_port, rx_ownership) { test_rx_ownership(); }
ZTEST(static_packet_port, tx_lease) { test_tx_lease(); }
ZTEST(static_packet_port, stop_reinit_exhaustion) { test_stop_reinit_exhaustion(); }
ZTEST_SUITE(static_packet_port, NULL, NULL, NULL, NULL, NULL);
#else
int main(void) {
  test_rx_ownership();
  test_tx_lease();
  test_stop_reinit_exhaustion();
  return 0;
}
#endif
