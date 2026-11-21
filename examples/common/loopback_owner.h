/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_EXAMPLE_LOOPBACK_OWNER_H
#define WIRELINK_EXAMPLE_LOOPBACK_OWNER_H
#include "wirelink/endpoint.h"
#include "wirelink/packet_loopback.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "example_check.h"
typedef struct {
  wl_endpoint_t *cores[2];
  wl_packet_loopback_t cable;
  wl_packet_io_t io[2];
  uint32_t now;
} example_pair_t;
static inline void example_pair_init(example_pair_t *pair, wl_endpoint_t *a, wl_endpoint_t *b,
                                     wl_packet_port_t *first, wl_packet_port_t *second) {
  memset(pair, 0, sizeof(*pair));
  pair->cores[0] = a;
  pair->cores[1] = b;
  EXAMPLE_REQUIRE(wl_packet_loopback_init(&pair->cable, first, second, NULL, NULL) == WL_OK);
  for (unsigned side = 0; side < 2; ++side) {
    pair->io[side] = (wl_packet_io_t){.submit = wl_packet_loopback_submit,
      .notify = wl_packet_loopback_notify, .user = &pair->cable.directions[side]};
  }
}
static inline void example_pair_step(example_pair_t *pair) {
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(wl_endpoint_step(pair->cores[side], pair->now, 16) == WL_OK);
  EXAMPLE_REQUIRE(wl_packet_loopback_service(&pair->cable, 2) == WL_OK);
  ++pair->now;
}
static inline void example_pair_wait(example_pair_t *pair, const unsigned *completed, unsigned target) {
  for (unsigned pass = 0; pass < 1000 && *completed < target; ++pass) example_pair_step(pair);
  EXAMPLE_REQUIRE(*completed == target);
}
static inline void example_pair_close(example_pair_t *pair) {
  const bool cancelled[2] = {pair->cable.directions[0].pending != 0, pair->cable.directions[1].pending != 0};
  for (unsigned side = 0; side < 2; ++side) EXAMPLE_REQUIRE(wl_endpoint_close(pair->cores[side]) == WL_OK);
  wl_packet_loopback_stop(&pair->cable);
  for (unsigned side = 0; side < 2; ++side) {
    const wl_err_t error = wl_endpoint_step(pair->cores[side], pair->now, 16);
    /* A stopped model driver reports a real abandoned physical transfer. */
    EXAMPLE_REQUIRE(error == WL_OK || (cancelled[side] && error == WL_ERR_IO));
    EXAMPLE_REQUIRE(wl_endpoint_quiescent(pair->cores[side]));
  }
}
/* Copy responses that the application reads after completion. */
#define EXAMPLE_COMPLETION_IMPL(name, type, on_success) \
  static unsigned name##_count; \
  static wl_rpc_result_t name##_result; \
  static void name##_completed(void *user, wl_rpc_result_t result, const type##_t *view) { \
    (void)user; name##_result = result; \
    if (result.outcome == WL_RPC_SUCCESS) { \
      EXAMPLE_REQUIRE(view != NULL); \
      on_success; \
    } \
    ++name##_count; \
  }
#define EXAMPLE_COMPLETION(name, type) \
  static type##_t name##_value; \
  EXAMPLE_COMPLETION_IMPL(name, type, name##_value = *view)
#define EXAMPLE_COMPLETION_STATUS(name, type) \
  EXAMPLE_COMPLETION_IMPL(name, type, (void)view)
#endif
