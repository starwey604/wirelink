/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/kernel.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
static wl_kernel_t core;
static wl_packet_port_t port;
static uint8_t rx[2][64], tx[9][32];
static uint64_t instance = 100;
static unsigned received;
static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  (void)user; (void)data; (void)length; (void)token; return WL_PACKET_SENT;
}
static wl_err_t route(wl_kernel_t *owner, const wl_kernel_frame_t *frame) {
  (void)owner; CHECK(frame->message_id == 23 && frame->length == 4); ++received; return WL_OK;
}
wl_kernel_t *owner_pass_prepare(wl_packet_notify_fn notify, void *user) {
  received = 0;
  wl_packet_config_t packet = {.instance = ++instance, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx), .submit = sink,
    .notify = notify, .user = user};
  CHECK(wl_packet_init(&port, &packet) == WL_OK);
  wl_kernel_config_t config = {.port = &port, .route = route};
  for (unsigned i = 0; i < 9; ++i) config.tx_storage[i] = (wl_span_t){tx[i], sizeof(tx[i])};
  CHECK(wl_kernel_init(&core, &config) == WL_OK); return &core;
}
void owner_pass_inject(void) {
  const uint8_t packet[] = {0xa4, 0, 0, 23, 1, 2, 3, 4};
  wl_packet_claim_t claim;
  CHECK(wl_packet_rx_claim(&port, sizeof(packet), &claim) == WL_OK);
  memcpy(claim.span.data, packet, sizeof(packet));
  CHECK(wl_packet_rx_commit(&port, &claim, sizeof(packet)) == WL_OK);
}
unsigned owner_pass_received(void) { return received; }
