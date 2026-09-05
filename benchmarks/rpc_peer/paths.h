/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_BENCHMARKS_RPC_PEER_PATHS_H_
#define WIRELINK_BENCHMARKS_RPC_PEER_PATHS_H_

#include <stdint.h>

#include "wirelink/rpc.h"

wl_rpc_err_t
benchmark_peer_observe_direct(wl_rpc_server_t *server, wl_rpc_peer_t *peer,
                              uint64_t session_id,
                              wl_rpc_peer_observation_t *observation);
wl_rpc_err_t
benchmark_peer_observe_guarded(wl_rpc_server_t *server, wl_rpc_peer_t *peer,
                               uint64_t session_id,
                               wl_rpc_peer_observation_t *observation);

#endif /* WIRELINK_BENCHMARKS_RPC_PEER_PATHS_H_ */
