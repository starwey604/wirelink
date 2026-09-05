/* SPDX-License-Identifier: Apache-2.0 */

#include "paths.h"

#include <inttypes.h>
#include <stdio.h>
#include <time.h>

#define ITERATIONS UINT32_C(100000000)

typedef wl_rpc_err_t (*benchmark_path_fn)(wl_rpc_server_t *, wl_rpc_peer_t *,
                                          uint64_t,
                                          wl_rpc_peer_observation_t *);

static double run_path(benchmark_path_fn path, wl_rpc_server_t *server,
                       wl_rpc_peer_t *peer, uint64_t session_id,
                       volatile int32_t *checksum) {
  wl_rpc_peer_observation_t observation = {0};
  const clock_t start = clock();
  uint32_t iteration;
  for (iteration = 0U; iteration < ITERATIONS; ++iteration)
    *checksum += path(server, peer, session_id, &observation);
  return (double)(clock() - start) * 1000000000.0 /
         ((double)CLOCKS_PER_SEC * (double)ITERATIONS);
}

int main(void) {
  wl_rpc_server_t server = {0};
  wl_rpc_server_pending_slot_t pending[1] = {0};
  wl_rpc_server_cache_slot_t cache[1] = {0};
  uint8_t response[32] = {0};
  const wl_rpc_server_config_t config = {
      .pending_slots = pending,
      .pending_slot_count = 1U,
      .cache_slots = cache,
      .cache_slot_count = 1U,
      .response_storage = response,
      .response_storage_size = sizeof(response),
      .response_capacity_per_slot = sizeof(response),
      .cache_policy = WL_RPC_CACHE_REJECT_NEW,
  };
  wl_rpc_peer_t peer = {0};
  wl_rpc_peer_observation_t observation = {0};
  const uint64_t session_id = UINT64_C(0x574c50455246);
  volatile int32_t checksum = 0;
  double direct_ns;
  double guarded_ns;

  if (wl_rpc_server_init(&server, &config) != WL_RPC_OK ||
      wl_rpc_peer_observe(&server, &peer, session_id, NULL, NULL,
                          &observation) != WL_RPC_OK)
    return 1;

  direct_ns = run_path(benchmark_peer_observe_direct, &server, &peer,
                       session_id, &checksum);
  guarded_ns = run_path(benchmark_peer_observe_guarded, &server, &peer,
                        session_id, &checksum);
  printf("iterations=%" PRIu32 " direct_ns=%.3f guarded_ns=%.3f speedup=%.2fx "
         "checksum=%" PRId32 "\n",
         ITERATIONS, direct_ns, guarded_ns, direct_ns / guarded_ns, checksum);
  return 0;
}
