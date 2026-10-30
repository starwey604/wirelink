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

Still pending: compact endpoint call/reply ownership, WLC-generated routing,
product USB adapter integration and the K1 end-to-end matrix. Passing these
port tests does not establish K1 completion or any performance improvement.
