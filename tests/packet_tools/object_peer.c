/* SPDX-License-Identifier: Apache-2.0 */
#include "application.h"
#include "packet_tool_owner.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
static wl_kernel_t core;
static wl_packet_port_t port;
static usb_sample_application_t app;
static uint8_t rx[2][2053], tx[9][2052], output[2052], last[2052];
static size_t output_length, last_length;
static int drop, stale;
static wl_packet_submit_t peer_send(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  (void)user; (void)token; CHECK(length <= sizeof(output));
  memcpy(output, data, length); output_length = length;
  return WL_PACKET_SENT; /* Real model driver copy into owned receiving storage. */
}
static void receive(const uint8_t *data, size_t length) {
  wl_packet_claim_t claim;
  CHECK(wl_packet_rx_claim(example_packet_tool_port(), length, &claim) == WL_OK);
  memcpy(claim.span.data, data, length);
  CHECK(wl_packet_rx_commit(example_packet_tool_port(), &claim, length) == WL_OK);
}
wl_packet_submit_t object_peer_exchange(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  (void)user; (void)token;
  wl_packet_claim_t claim;
  CHECK(wl_packet_rx_claim(&port, length, &claim) == WL_OK);
  memcpy(claim.span.data, data, length);
  CHECK(wl_packet_rx_commit(&port, &claim, length) == WL_OK);
  output_length = 0;
  for (unsigned i = 0; i < 10; ++i) CHECK(usb_sample_pass(&app, &core, 0) == WL_OK);
  CHECK(output_length != 0);
  if (drop > 0) { --drop; return WL_PACKET_SENT; }
  if (stale && last_length) receive(last, last_length);
  receive(output, output_length);
  memcpy(last, output, output_length); last_length = output_length;
  return WL_PACKET_SENT;
}
void object_peer_prepare(void) {
  wl_packet_config_t packet = {.instance = 9090, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx), .submit = peer_send};
  CHECK(wl_packet_init(&port, &packet) == WL_OK);
  wl_kernel_config_t config = {.port = &port, .route = usb_sample_route, .user = &app};
  for (unsigned i = 0; i < 9; ++i) config.tx_storage[i] = (wl_span_t){tx[i], sizeof(tx[i])};
  CHECK(wl_kernel_init(&core, &config) == WL_OK);
}
void object_peer_faults(int count, int old) { drop = count; stale = old; }
unsigned object_peer_commits(void) { return app.commits; }
unsigned object_peer_writes(void) { return app.writes; }
void object_peer_close(void) {
  usb_sample_close(&app); CHECK(wl_kernel_close(&core) == WL_OK);
  CHECK(wl_kernel_quiescent(&core));
}
