# Four concurrent RPC calls

Build `wirelink_calculator_demo` with `WIRELINK_BUILD_WLC_EXAMPLES=ON` and WLC
0.14.0. `calculator.wl` defines one RPC, an embedded `client` and an embedded
`server`. Implement the two packet-write hooks and `server_on_add`; no handler
table or separate port allocation is needed. Four calls coexist, snapshot their
requests during admission, and finish through typed callbacks. The blocking
application loop also checks a business rejection for integer overflow.

`wirelink_rpc_lifecycle` uses the explicit external/dynamic node in
`lifecycle/calculator_fixture.wl`. This advanced fixture checks a fifth admission
returning `WL_ERR_WOULD_BLOCK`, local cancellation, three queued timeouts, a TX
lease surviving its call, recovery, and reverse-direction RPC rejection. It keeps
the physical lease model outside the simple teaching schema. Its deterministic
tick is one owner pass; both examples run in RAM without hardware.

The simple call starts its timeout at the next owner step. `_ex` accepts an
explicit timestamp, timeout, callback context and optional cancellation handle.
Local timeout/cancel cannot undo remote effects; applications own retries and
idempotency. See [Wirelink documentation](../../docs/README.md).
