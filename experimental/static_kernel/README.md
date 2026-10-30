# Static endpoint kernel prototype

This is the isolated K1 work area for the [refactor plan](../../docs/static-endpoint-kernel-refactor-plan-20261030-cn.md).
It is not an installed product mode. Its standalone CMake target does not link
the old Wirelink library.

The first component is the allocation-free C11 packet port. A serialized I/O
producer claims, commits or aborts RX storage; the owner borrows and releases
complete packets. The caller supplies exactly two RX units. Commit validates
the capacity saved at claim time, and a borrow keeps its unit unavailable to
the producer. No codec or business callback runs in the I/O context.

TX has one physical transfer at a time. `CONSUMED` permits reuse of source
bytes; `DRAINED` permits another transfer. A sink returning `BUSY` or `FAILED`
has accepted no bytes and must not subsequently publish a completion. A sink
returning `STARTED` may publish completion before returning. On `BUSY`, the
adapter publishes activity when resources become available; an owner must
wait for that notification rather than spin on the pending message.

Tokens contain a unique nonzero instance ID and a nonwrapping sequence. TX
sequence and completion flags share one lock-free atomic word, so a delayed
old publisher cannot update a newer transfer between its token check and
publication. RX sequences exhaust at `UINT32_MAX`; TX sequences exhaust at
`WL_PACKET_SEQUENCE_MAX` (28 bits). Exhaustion requires a quiescent restart
with a new instance ID, not wrapping IDs. Ordinary ring cursors may wrap.

`stop` rejects new claims and submits and wakes the owner. Existing reads may
abort, queued RX may be drained, and existing TX must finish or be explicitly
aborted by the adapter. `quiescent` checks the port's outstanding ownership;
the adapter must additionally acknowledge that all I/O callbacks have stopped
before reinitializing or destroying the object. Initialization, owner calls
and producer calls must follow the roles documented in the header. The outer
runtime supplies a latched event for its query/wait race; the port's activity
bit can be atomically consumed without losing a concurrent publication.

Build and test:

```sh
cmake -S experimental/static_kernel -B build/static-kernel
cmake --build build/static-kernel --parallel
ctest --test-dir build/static-kernel --output-on-failure
```

The deterministic fixture also runs as the Ztest `type: unit` suite
`tests/zephyr/unit/static_packet_port`. The host concurrency fixture transfers
100,000 RX/TX units between an I/O thread and an owner, including late duplicate
completion publication racing the next submit.

The shared endpoint kernel now owns one four-call table, four request buffers,
four reply buffers and one ordinary-message buffer supplied by generated code.
Requests are snapshotted before admission returns. Terminal callbacks release
logical slots; leased TX bytes remain reserved until physical drain. A callback
may submit/cancel calls, but recursive step/close fails. Generation snapshots
exclude callback-created calls from the current deadline pass.

Reply reservation precedes decoding and handler side effects. BUSY retains the
encoded reply and sleeps until activity. A full reply pool retains the RX
borrow. There is no duplicate history: independently admitted duplicate requests
may execute again. Errors are returned from step and optionally sent to a small
diagnostic callback. The owner supplies time; deadlines use bounded u32 modular
arithmetic. Close schedules one terminal outcome per still-waiting call and
continues draining physical completion before quiescence.

The shared-kernel unit fixture uses a deliberately small test codec; it does
not establish generated business-codec or USB interoperability. Still pending:
WLC-generated routing, product USB integration and the K1 end-to-end matrix.
Passing these tests does not establish K1 completion or performance improvement.

The native/NONE connector now writes the four-byte ordinary DATA prefix in
place and validates incoming packets without a general frame encoder, selector,
CRC or COBS dependency. Managed RPC metadata uses the existing v4 session,
tagged operation and rejection encoding. It supplies wire primitives only;
there is no replay, ARQ or server execution layer here.

`kernel_wire` tests include 50 vectors generated from frozen old-A
`managed_rpc_helpers.c.in` (SHA-256
`60f79894bd57c2e0e30b9da4618d087b6dd4caf94eae16d73a9c8d36afcdf8d0`),
truncated/noncanonical/overflow metadata, signed rejection statuses, native
payload bounds and unsupported reliable frames followed by ordinary DATA.
The same tests run in Ztest. External K1 evidence additionally compares one
million metadata cases and all 2,049 legal native payload sizes against frozen
A's helpers and framing tool. The Xtensa `-Os` native connector has no undefined
symbols; this is a code dependency check, not a device performance measurement.
