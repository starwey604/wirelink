/* SPDX-License-Identifier: Apache-2.0 */

#include "paths.h"

wl_rpc_err_t
benchmark_peer_observe_direct(wl_rpc_server_t *server, wl_rpc_peer_t *peer,
                              uint64_t session_id,
                              wl_rpc_peer_observation_t *observation) {
  return wl_rpc_peer_observe(server, peer, session_id, NULL, NULL, observation);
}

wl_rpc_err_t
benchmark_peer_observe_guarded(wl_rpc_server_t *server, wl_rpc_peer_t *peer,
                               uint64_t session_id,
                               wl_rpc_peer_observation_t *observation) {
  if (peer->session_id != session_id)
    return wl_rpc_peer_observe(server, peer, session_id, NULL, NULL,
                               observation);
  return WL_RPC_OK;
}
