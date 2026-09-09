# Product policy on a generated endpoint

Use the generated endpoint for storage, dispatch, RPC ownership and its clock.
Use `wl_endpoint_set_policy()` only when a product needs owner-local work such
as a control lease or a cross-thread slow-operation completion queue. Install
the descriptor before starting the owner; its context must outlive the owner.

Each step reads the endpoint clock once, then runs:

1. Adapter service (including product pre-dispatch safety deadlines).
2. Bounded event polling. Policy observes each event **before** generated
   dispatch. It must not release that event or take its TX handle.
3. Policy progress, then generated runtime/RPC service.

Policy progress must not wait or run another event loop. Report follow-up work
only when another pass is needed; consuming the current work alone is not a
reason. The policy deadline is merged with generated and transport deadlines.
No callback may recursively step or close the endpoint.

Adapter quiesce stops producers before generated close cancels and delivers
accepted client calls. Keep call contexts alive until close completes. For
slow server work, copy the generated typed token and business input; do not
reconstruct tokens from operation IDs. The advanced `*_request_inspect()` API
provides incarnation-checked identity for diagnostics and a product's deadline
table, not a guarantee that a reservation remains pending.

COBS deployments can define `<RUNTIME>_ENDPOINT_RX_FIFO_CAPACITY` and
`<RUNTIME>_ENDPOINT_RPC_CAPACITY` once on their CMake runtime target. Propagate
them to every translation unit using the generated type. Do not access
`private_state` or reproduce its buffers in application classes.
