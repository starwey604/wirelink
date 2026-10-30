/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_RPC_INTERNAL_H
#define WIRELINK_RPC_INTERNAL_H
#include "wirelink/rpc.h"

/* Single-owner inspection with the same owner/generation checks as get_by_handle.
 * Readiness needs no completion metadata or typed-result index. */
wl_rpc_err_t wl_rpc_client_state_by_handle(
    const wl_rpc_client_t *client, const wl_rpc_client_handle_t *handle,
    wl_rpc_client_state_t *out_state);
#endif
