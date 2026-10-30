/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_EXAMPLE_LOOPBACK_OWNER_H
#define WIRELINK_EXAMPLE_LOOPBACK_OWNER_H
#include "wirelink/kernel.h"
#include "wirelink/packet_loopback.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define EXAMPLE_REQUIRE(condition) do { if (!(condition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); abort(); \
} } while (0)
typedef struct {
  wl_packet_port_t ports[2];
  wl_kernel_t *cores[2];
  wl_packet_loopback_t cable;
  uint8_t rx[2][2][2053];
  uint32_t now;
} example_pair_t;
static inline void example_pair_init(example_pair_t *pair, wl_kernel_t *a, wl_kernel_t *b) {
  static uint64_t next_instance = 1000; /* Single-owner fixture provider. */
  memset(pair, 0, sizeof(*pair)); pair->cores[0] = a; pair->cores[1] = b;
  EXAMPLE_REQUIRE(wl_packet_loopback_init(&pair->cable, &pair->ports[0], &pair->ports[1], NULL, NULL) == WL_OK);
  for (unsigned side = 0; side < 2; ++side) {
    /* Deterministic fixture identities. Production must supply fresh identities. */
    EXAMPLE_REQUIRE(next_instance != UINT64_MAX);
    wl_packet_config_t config = {.instance = ++next_instance,
      .rx_storage = &pair->rx[side][0][0], .rx_storage_size = sizeof(pair->rx[side]),
      .rx_unit_size = sizeof(pair->rx[side][0]), .submit = wl_packet_loopback_submit,
      .notify = wl_packet_loopback_notify, .user = &pair->cable.directions[side]};
    EXAMPLE_REQUIRE(wl_packet_init(&pair->ports[side], &config) == WL_OK);
  }
}
static inline void example_pair_step(example_pair_t *pair) {
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(wl_kernel_step(pair->cores[side], pair->now, 16) == WL_OK);
  EXAMPLE_REQUIRE(wl_packet_loopback_service(&pair->cable, 2) == WL_OK);
  ++pair->now;
}
static inline void example_pair_wait(example_pair_t *pair, const unsigned *completed, unsigned target) {
  for (unsigned pass = 0; pass < 1000 && *completed < target; ++pass) example_pair_step(pair);
  EXAMPLE_REQUIRE(*completed == target);
}
static inline void example_pair_close(example_pair_t *pair) {
  const bool cancelled[2] = {pair->cable.directions[0].pending != 0, pair->cable.directions[1].pending != 0};
  for (unsigned side = 0; side < 2; ++side) EXAMPLE_REQUIRE(wl_kernel_close(pair->cores[side]) == WL_OK);
  wl_packet_loopback_stop(&pair->cable);
  for (unsigned side = 0; side < 2; ++side) {
    const wl_err_t error = wl_kernel_step(pair->cores[side], pair->now, 16);
    /* A stopped model driver reports a real abandoned physical transfer. */
    EXAMPLE_REQUIRE(error == WL_OK || (cancelled[side] && error == WL_ERR_IO));
    EXAMPLE_REQUIRE(wl_kernel_quiescent(pair->cores[side]));
  }
}
/* Retain only application-owned results; no call matching or timeout table. */
#define EXAMPLE_COMPLETION(name, type) \
  static unsigned name##_count; \
  static wl_kernel_result_t name##_result; \
  static type##_value_t name##_value; \
  static void name##_completed(void *user, wl_kernel_result_t result, const type##_t *view) { \
    (void)user; name##_result = result; \
    if (result.outcome == WL_KERNEL_SUCCESS) \
      EXAMPLE_REQUIRE(view && type##_value_from_view(view, &name##_value) == WL_CODEC_OK); \
    ++name##_count; \
  }
#endif
