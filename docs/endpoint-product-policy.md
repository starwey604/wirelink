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
4. Statically bound `wl_endpoint_service_t` progress callbacks, in rotating order.

`wl_endpoint_set_services()` borrows one immutable array before the first step.
Its deadline hints merge with the existing hints. Session hooks see nonzero RX
sessions before dispatch (first observation has previous session zero); use them
to reset bulk/application state and call generated `*_runtime_peer_observe()`
when the first new-session message can be non-RPC. Unreliable compact-v1 DATA
does not carry a session; do not infer a peer reboot from such traffic.
Close invokes service cleanup in reverse order after adapter quiescence.
See [the runnable composition example](../examples/04_composed_services/README.md).

Rotation is cooperative scheduling between these callbacks, not a bandwidth
reservation. RPC retains first admission to the shared reliable TX slot. Bound
RPC admission and telemetry during upload in product policy; an unbounded RPC
producer can starve bulk. A blocked callback must not report immediate work
without a possible transition: use transport wakeups or a future deadline.

Policy progress must not wait or run another event loop. Report follow-up work
only when another pass is needed; consuming the current work alone is not a
reason. The policy deadline is merged with generated and transport deadlines.
No callback may recursively step or close the endpoint.

Generated drivers mark readiness as complete. Executors therefore consult the
merged hint instead of requesting another pass just because RX was consumed.
Custom drivers leave `readiness_complete` zero unless their endpoint hint also
accounts for all driver-local follow-up work.

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
