/* SPDX-License-Identifier: Apache-2.0 */
#include "instrument_kernel.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
typedef struct peer {
  instrument_kernel_t endpoint;
  wl_packet_port_t port;
  uint8_t rx[2][256], frame[128];
  size_t length;
  unsigned received, errors;
} peer_t;
static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t length,
                               wl_packet_token_t token) {
  peer_t *p = user; (void)token;
  CHECK(length <= sizeof(p->frame));
  memcpy(p->frame, data, length); p->length = length;
  return WL_PACKET_SENT;
}
static void reading(void *user, const reading_t *view) {
  peer_t *p = user;
  reading_value_t owned;
  CHECK(reading_value_from_view(view, &owned) == WL_CODEC_OK);
  CHECK(owned.axis.coordinate == INT32_MIN && owned.axis.label.length == 7);
  CHECK(!memcmp(owned.axis.label.data, "a\0bcdef", 7));
  CHECK(owned.samples[0] == 0 && owned.samples[1] == UINT32_MAX && owned.samples[2] == 42);
  CHECK(owned.note.length == 17 && owned.note.data[16] == 16);
  ++p->received;
}
static void error(void *user, uint16_t id, wl_err_t e) {
  peer_t *p = user; CHECK(id == 201 && e != WL_OK); ++p->errors;
}
static void init(peer_t *p, uint64_t instance) {
  wl_packet_config_t c = {.instance = instance, .rx_storage = &p->rx[0][0],
      .rx_unit_size = 256, .rx_storage_size = sizeof(p->rx), .submit = sink, .user = p};
  instrument_kernel_handlers_t h = {.on_message_reading = reading};
  CHECK(wl_packet_init(&p->port, &c) == WL_OK);
  CHECK(instrument_kernel_init(&p->endpoint, &p->port, &h, error, p) == WL_OK);
}
static void inject(peer_t *to, const uint8_t *bytes, size_t length) {
  wl_packet_claim_t claim;
  CHECK(wl_packet_rx_claim(&to->port, 256, &claim) == WL_OK);
  memcpy(claim.span.data, bytes, length);
  CHECK(wl_packet_rx_commit(&to->port, &claim, length) == WL_OK);
}
int main(void) {
  peer_t a = {0}, b = {0}; init(&a, 101); init(&b, 102);
  reading_value_t value = {.has_axis = true, .axis = {.has_coordinate = true,
      .coordinate = INT32_MIN, .has_label = true, .label = {.length = 7, .data = {'a',0,'b','c','d','e','f'}}},
      .has_samples = true, .samples = {0,UINT32_MAX,42}, .has_note = true, .note = {.length = 17}};
  for (unsigned i = 0; i < 17; ++i) value.note.data[i] = (uint8_t)i;
  uint8_t golden[128]; size_t n = 0;
  CHECK(reading_value_encode(&value, golden + 4, sizeof(golden) - 4, &n) == WL_CODEC_OK);
  CHECK(wl_kernel_native_none_seal(golden, sizeof(golden), 201, n) == WL_OK);
  CHECK(instrument_kernel_send_reading(&a.endpoint, &value) == WL_OK);
  memset(&value, 0, sizeof(value));
  CHECK(instrument_kernel_step(&a.endpoint, 0) == WL_OK);
  CHECK(a.length == n + 4 && !memcmp(a.frame, golden, n + 4));
  inject(&b, a.frame, a.length); CHECK(instrument_kernel_step(&b.endpoint, 0) == WL_OK);
  // Unknown optional field retained in input semantics, not a new value-domain limit.
  a.frame[a.length++] = 0x78; a.frame[a.length++] = 0x7f;
  inject(&b, a.frame, a.length); CHECK(instrument_kernel_step(&b.endpoint, 0) == WL_OK);
  CHECK(b.received == 2);
  // Truncate inside the required nested message, then recover on the next packet.
  inject(&b, a.frame, 6); CHECK(instrument_kernel_step(&b.endpoint, 0) != WL_OK);
  CHECK(b.received == 2 && b.errors == 1);
  inject(&b, golden, n + 4); CHECK(instrument_kernel_step(&b.endpoint, 0) == WL_OK);
  inject(&a, golden, n + 4); CHECK(instrument_kernel_step(&a.endpoint, 0) == WL_OK);
  CHECK(b.received == 3 && a.received == 1 && INSTRUMENT_KERNEL_MESSAGE_FRAME_MAX == 54);
  CHECK(wl_kernel_close(&a.endpoint.core) == WL_OK && instrument_kernel_step(&a.endpoint, 0) == WL_OK);
  CHECK(wl_kernel_close(&b.endpoint.core) == WL_OK && instrument_kernel_step(&b.endpoint, 0) == WL_OK);
  CHECK(wl_kernel_quiescent(&a.endpoint.core) && wl_kernel_quiescent(&b.endpoint.core));
}
