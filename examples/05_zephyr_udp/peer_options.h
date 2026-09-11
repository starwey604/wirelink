/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_EXAMPLE_PEER_OPTIONS_H_
#define WIRELINK_EXAMPLE_PEER_OPTIONS_H_

#include "tutorial_host.h"

/* Example CLI plumbing only, not an endpoint/storage abstraction. */
static inline example_udp_t *peer_open(wl_endpoint_t *endpoint, int argc, char **argv) {
  int32_t local, peer;
  if (argc != 5 || !example_int32(argv[2], &local) || !example_int32(argv[4], &peer) ||
      local < 1 || local > UINT16_MAX || peer < 1 || peer > UINT16_MAX) {
    fprintf(stderr, "usage: %s LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT\n", argv[0]);
    return NULL;
  }
  return example_udp_open_at(endpoint, argv[1], (uint16_t)local, argv[3], (uint16_t)peer);
}
#endif
