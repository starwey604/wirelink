# Four concurrent RPC calls

Build `wirelink_calculator_demo` with `WIRELINK_BUILD_WLC_EXAMPLES=ON` and matching
WLC 0.13.0 / contract 0.13. Four calls snapshot their requests before the source
is cleared; one application owner loop advances both endpoints. A reverse call
checks both RPC roles and distinguishes business rejection from local failure.

`wirelink_rpc_lifecycle` exercises a fifth admission returning `WL_ERR_WOULD_BLOCK`,
local cancellation, three queued timeouts, a TX lease surviving its call and
successful reuse. Its deterministic tick is one loop pass, not milliseconds.
Both examples use RAM-copy loopback, without hardware.

Follow the [RPC tutorial](../../docs/tutorial-rpc.md) and
[cancellation/capacity tutorial](../../docs/tutorial-rpc-async.md) for commands
and build-backed source. For longer work, use an
[application job protocol](../../docs/tutorial-jobs.md). Local timeout/cancel
cannot undo remote effects; applications own retries and idempotency.
