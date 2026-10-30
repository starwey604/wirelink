/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_port.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)
#define COUNT 100000U

static wl_packet_port_t port;
static uint8_t storage[2][32];
static atomic_uint_least32_t submitted;

static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t size,
                               wl_packet_token_t token) {
  (void)user;
  CHECK(size == 4U && data != NULL);
  atomic_store_explicit(&submitted, token.sequence, memory_order_release);
  return WL_PACKET_STARTED;
}

static void *io_thread(void *unused) {
  (void)unused;
  for (uint32_t i = 1U; i <= COUNT; ++i) {
    wl_packet_claim_t claim;
    wl_err_t status;
    do { status = wl_packet_rx_claim(&port, sizeof(i), &claim); }
    while (status == WL_ERR_WOULD_BLOCK);
    CHECK(status == WL_OK);
    memcpy(claim.span.data, &i, sizeof(i));
    CHECK(wl_packet_rx_commit(&port, &claim, sizeof(i)) == WL_OK);
    while (atomic_load_explicit(&submitted, memory_order_acquire) < i) {}
    wl_packet_token_t token = {77U, i};
    CHECK(wl_packet_tx_publish(&port, token, WL_PACKET_CONSUMED) == WL_OK);
    CHECK(wl_packet_tx_publish(&port, token, WL_PACKET_DRAINED) == WL_OK);
    /* Race a duplicate old completion against the next owner's submit. */
    CHECK(wl_packet_tx_publish(&port, token, WL_PACKET_DRAINED) == WL_ERR_NOT_FOUND);
  }
  return NULL;
}

int main(void) {
  wl_packet_config_t config = {
      .instance = 77U, .rx_storage = &storage[0][0],
      .rx_unit_size = sizeof(storage[0]), .rx_storage_size = sizeof(storage),
      .submit = sink
  };
  CHECK(wl_packet_init(&port, &config) == WL_OK);
  atomic_init(&submitted, 0U);
  pthread_t io;
  CHECK(pthread_create(&io, NULL, io_thread, NULL) == 0);
  for (uint32_t i = 1U; i <= COUNT; ++i) {
    wl_packet_borrow_t borrow;
    wl_err_t status;
    do { status = wl_packet_rx_borrow(&port, &borrow); }
    while (status == WL_ERR_NO_DATA);
    CHECK(status == WL_OK);
    uint32_t value;
    memcpy(&value, borrow.data, sizeof(value));
    CHECK(value == i && borrow.length == sizeof(value));
    wl_packet_token_t token;
    CHECK(wl_packet_tx_submit(&port, borrow.data, borrow.length, &token) == WL_OK);
    unsigned events = 0U;
    while ((events & WL_PACKET_DRAINED) == 0U) events |= wl_packet_tx_take(&port, &token);
    CHECK((events & WL_PACKET_CONSUMED) != 0U);
    CHECK(wl_packet_rx_release(&port, &borrow) == WL_OK);
  }
  CHECK(pthread_join(io, NULL) == 0);
  wl_packet_stop(&port);
  CHECK(wl_packet_quiescent(&port));
  return 0;
}
