# How the kernel works

[中文](kernel-cn.md) · [API reference](kernel-api.md) · [Run telemetry](getting-started.md)

Wirelink connects two endpoints with typed ordinary messages and bounded RPC.
You supply storage, one owner, time snapshots and a transport. The C11 kernel
dispatches input, queues encoded output and completes calls without allocating,
reading a clock or starting a thread.

## Run two endpoints first

From the repository root, build the checked-in generated example:

```sh
cmake -S . -B /tmp/wirelink-docs-core -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/wirelink-docs-core --parallel
/tmp/wirelink-docs-core/examples/wirelink_bare_metal_loopback
ctest --test-dir /tmp/wirelink-docs-core --output-on-failure
```

The executable prints:

```text
8 bidirectional RPC completions, 2 telemetry messages; both endpoints quiescent
```

[`bare_metal_loopback.c`](../examples/bare_metal_loopback.c) runs both endpoints
on one owner with a RAM-copy transport. Its time unit is one loop iteration;
`500U` is a budget of 500 ticks, not 500 milliseconds. It needs no WLC executable.
Use the [telemetry](getting-started.md) and [RPC](tutorial-rpc.md) tutorials to
generate endpoints from your own schema with matching local WLC.

## From schema to physical I/O

1. **Schema and codegen** define message fields and IDs. A profile-v2 binding
   selects routes, RPC request/response pairs and one connector. WLC emits
   codecs, typed handlers, route dispatch and bounded endpoint storage.
2. **Route and kernel** encode accepted values into snapshots. The kernel
   schedules calls, terminals, RX and TX. The generated route decodes borrowed
   input and invokes the appropriate typed handler on the owner.
3. **Packet port and connector** exchange canonical native/NONE DATA packets.
   A stream connector adds COBS/CRC16 or length16/NONE outside this boundary.
4. **Adapter** moves packets or bytes through the actual driver and publishes
   input, resource activity and token-bound physical completion.

The generated endpoint contains the shared `wl_kernel_t`, not a second RPC
engine. Host Executor, C++ Session and generated SDKs use the same kernel.
Standalone codecs and FIFO/LATEST/outbox containers can also be used separately.
See [profiles](link-profiles.md), [wire format](packet-protocol.md) and
[adapter lifecycle](adapters.md) for their respective contracts.

## One owner executes application work

The owner serializes admission, cancellation, `step`, readiness queries and
close. It also executes message handlers, RPC handlers, terminal callbacks and
`on_error`. An owner may be a bare-metal loop, an RTOS task or a host thread.
Multiple endpoints may share it.

An I/O producer publishes through the packet port. RX has one serialized
producer and one owner consumer; separate driver callbacks must serialize their
RX claim/commit/abort operations. They do not run codecs or application
handlers. For serial staging, an I/O producer may instead fill the C-owned byte
FIFO, which the owner later parses into packets.

`notify` is the exception to application callback execution: it can run in I/O
context and must only signal a suitable latched wake. The sink is called
synchronously by owner TX submission; it may publish completion inline, but
must not reenter kernel execution.

### Storage has several different capacities

The fixed logical limits are defined in the [API capacity reference](kernel-api.md#capacities).
Each endpoint has four outbound call slots, four request snapshots, four reply
snapshots, one ordinary-message snapshot and a nine-index admission FIFO. RX
has two units; the port permits one physical TX at a time.

These limits count different things. A call slot can become free at timeout
while its request snapshot remains leased to I/O. Four free call slots therefore
do not necessarily mean four available request snapshots. The nine-index FIFO
orders requests, replies and ordinary messages by admission; it stores indices,
not another set of payloads.

WLC sizes request, reply and ordinary storage separately from the selected codec
bounds. The generated `*_KERNEL_PACKET_FRAME_MAX` and
`*_KERNEL_WIRE_FRAME_MAX` describe byte capacity, not concurrency. RX units,
stream TX scratch, byte FIFOs and hardware/DMA buffers have their own byte
capacities. Hardware buffering does not increase the kernel's call limit.

C defines packet-port and byte-FIFO atomic storage. C++ receives opaque pointers;
`std::atomic` cannot replace that storage. Keep the endpoint, port, RX units,
connector storage, adapter and wake context alive through shutdown.

## Follow an ordinary message

The checked-in demo sends `event` with message ID `42`:

1. `demo_kernel_send_event` encodes the supplied `event_value_t` into the
   ordinary-message snapshot before returning `WL_OK`. The caller can then
   overwrite its source, including source bytes passed through a view API.
2. A subsequent owner `step` submits the queued canonical DATA packet to the
   port's sink. Admission is not physical delivery. If the ordinary snapshot is
   still occupied, another send returns `WL_ERR_WOULD_BLOCK` without admitting
   a message; ordinary sends have no terminal callback.
3. The loopback adapter retains the source, copies it into a peer RX claim on
   service, commits the packet and publishes `DRAINED` to the sending port.
   With other transports, the adapter's real completion boundary applies.
4. The receiving owner borrows the committed packet. Generated routing decodes
   it and invokes `on_message_event`. The view and any referenced bytes are
   borrowed until the handler returns; copy values needed later.
5. The receiver releases RX. The sender consumes completion in `step` and frees
   the snapshot at physical drain.

Unknown message IDs report `WL_ERR_NOT_SUPPORTED` through `on_error` when
configured. Malformed packets are released before reporting the error so the
next packet can be processed.

## Follow a successful RPC

In the same demo, `execute` uses request ID `40` and response ID `41`:

1. `demo_kernel_execute_start` takes an owned request value, `now`, a timeout,
   a typed callback/context and an output handle. On `WL_OK`, the request has
   been encoded and a call with deadline `now + timeout` exists. Any admission
   failure creates no call and no future terminal callback.
2. Owner TX submission sends the snapshot, including the client's instance and
   operation identity encoded at admission. A successfully submitted request
   becomes eligible for response matching.
3. The peer route validates RPC metadata, **reserves reply storage before
   business decoding and handler effects**, decodes the request and calls the
   synchronous handler. The request view is borrowed; the response value is
   endpoint-owned scratch valid for this handler invocation.
4. The handler fills the response and returns `0`. The route encodes the reply,
   preserving client session/operation, and queues it. A nonzero signed status
   instead encodes a business rejection with no business body.
5. If the sink returns `BUSY`, the encoded reply remains queued. Resource
   activity permits another submission attempt; the handler is not rerun for
   that retained reply. A separately received duplicate request may execute
   again: the kernel has no replay cache.
6. The client's owner matches response ID, instance and operation against a
   waiting, submitted call. Generated completion decodes the response once and
   invokes the typed callback. The logical slot is released before callback
   entry; its handle is no longer valid. Duplicate, stale and unmatched valid
   responses do not complete another call.

Each admitted call receives one terminal callback as the owner continues
stepping. The [result reference](kernel-api.md#terminal-results) defines success,
rejection, local error, timeout, cancellation and close. A success view is valid
only until callback return. Use public `*_value_from_view` conversion to retain
strings, bytes and nested values; see [retaining results](tutorial-rpc-values.md).

If reply encoding fails after a handler effect, the peer reports a local error;
that error is not automatically returned as a business rejection. The caller may
eventually time out. Design effects and application-level recovery accordingly.

## Scheduling uses events and caller time

`wl_kernel_step(core, now, budget)` supports `1..WL_KERNEL_BUDGET` events;
the current maximum is `16`. One observed physical-completion batch, one
terminal delivery, one TX submission attempt or one RX processing attempt
consumes one event. An RX attempt that discovers reply pressure also counts;
repeated passes do not retry it until reply storage is available. A `BUSY` TX
attempt counts and is not retried without port activity.

Each scheduling iteration handles physical completion first, then eligible
terminals before RX. If several calls are due, they all precede RX within the
available budget; a small budget leaves the remainder for later passes. The
call-generation snapshot at step entry excludes callback-created calls from
that step's terminal scan. A callback may submit or cancel calls, including
another existing call; recursive `step` or `close` returns `WL_ERR_REENTRANT`.
Callbacks must return promptly so other owner work can proceed.

RX/TX preference persists across passes. After TX submission, runnable RX gets
a turn before the next submission, even when a completion used the preceding
pass's whole budget. Empty or reply-pressure-blocked RX does not prevent TX;
continuous RX does not remove TX's opportunity. This ensures progress with
`budget=1`, provided the outer owner keeps running and I/O makes progress.

### Deadlines include queue residence

The owner supplies monotonic modular `uint32_t` ticks. Timeout is
`1..INT32_MAX` ticks and starts at admission, including time spent queued or
waiting for a busy transport. The core does not assign a real-time unit. Host
Executor uses milliseconds; deterministic loopback examples use loop ticks.
Custom owners must use one consistent unit for admission, step and waits.

Expiry uses `(uint32_t)(now - deadline) <= INT32_MAX`, so equality is expired
and a deadline can cross `UINT32_MAX`. Keep observations within the modular
half-range; a call must not remain unexamined more than `INT32_MAX` ticks past
its deadline. At the exact deadline, timeout wins over a response still in RX,
even if the packet arrived earlier. There is no I/O arrival timestamp decision.

## Follow timeout while physical TX is still active

Suppose a call starts at tick `100` with timeout `10`:

1. At `100`, the sink returns `STARTED` and retains request bytes.
2. At `110`, owner `step` delivers `TIMED_OUT`. The handle expires and the
   logical call slot is freed, but the leased request snapshot stays intact.
3. A later `CONSUMED` publication tells the port the source is no longer read.
   The kernel still retains that snapshot until `DRAINED`; no second physical
   TX starts merely because the source was consumed.
4. At actual `DRAINED` (or `TX_ERROR` after real completion/cancellation), owner
   `step` releases the physical flight and its snapshot. A late response cannot
   deliver another terminal for the expired operation.

Cancellation has the same separation of logical and physical lifetimes. Timeout
and cancellation are local outcomes; neither stops a peer's side effects.
Retrying creates a new operation. Use application request IDs and idempotency
when repeating an effect would be unsafe. Long work can return a job ID and be
queried later; see [application jobs](tutorial-rpc-deferred.md).

## Backpressure travels through storage

When all reply snapshots are occupied, routing retains the current RX borrow
without running its business handler. RX is FIFO: that retained head packet
also delays packets behind it. Once both RX units are occupied, another claim
returns `WL_ERR_WOULD_BLOCK`.

A stream feed reports how much input it consumed. On pressure, retain the
unconsumed suffix and retry after space becomes available. The optional SPSC
byte FIFO stages bytes, including with non-power-of-two capacity. Ordinary
pressure pauses/resumes input; actual byte loss requires explicit gap reporting
and stream resynchronization. See [`packet_stream.h`](../include/wirelink/packet_stream.h)
and [`byte_fifo.h`](../include/wirelink/byte_fifo.h).

For native USB, RX also needs strict read headroom, exact-MPS/ZLP handling and
real reset/reconfiguration after a boundary fault. For UART, source consumption
is separate from actual transmitter drain. Keep these physical requirements in
the [adapter configuration](adapters.md), not in application timeout logic.

## Wait without losing activity

`wl_kernel_ready` answers whether core work is runnable and reports the earliest
future client deadline. It does not service an adapter or perform an OS wait.
The outer loop must:

1. Service the adapter and step the kernel with a time snapshot.
2. Sample time again; combine core readiness, adapter readiness and application
   work/deadlines. Continue immediately if any work is runnable.
3. Use a latched event so a notification between query and wait remains visible.
   Recheck work/activity across that transition without clearing a new wake.
4. Recompute a finite wait's remaining time after owner work. In the host
   runtime, adapter wait hints are in milliseconds, matching its core tick.

The port activity bit wakes blocked TX retries; it is not itself an OS wait
primitive. An `on_error` or terminal count is also not a physical-completion
test. [`Executor::run`](../runtime/host/src/executor.cpp) is a working host loop.

Executor has a 32-slot external command queue and executes at most eight
commands per batch before kernel work. Posting a command and admitting its RPC
are separate capacity decisions. Session/SDK wrappers supply the owner and
own copied application results; they do not add another kernel call table.
See [Session](session.md) and [SDK examples](../examples/06_bindings/README.md).

## Close, finish I/O, then release storage

1. On the owner, `wl_kernel_close` stops port admission, drops unleased output
   and marks waiting calls `CLOSED`. Already pending terminal outcomes are kept.
   Close schedules callbacks; it does not invoke them inline.
2. Stop the actual adapter. Cancel/join driver operations, return claimed or
   DMA buffers, and publish final token-bound completion only after the adapter
   has truly stopped using the source. Some adapters require continued service
   to reach their asynchronous stop acknowledgement.
3. Continue owner `step` calls to deliver terminals, discard queued RX and take
   final TX completion. Require `wl_kernel_quiescent`.
4. Also require adapter stop/join acknowledgement and callback/worker exit.
   Only then release the endpoint, port, RX, stream, driver and wake storage.

Kernel quiescence describes core/port state, not an external thread's exit.
Owner callbacks can request an outer stop, but must not join or destroy their
own Executor/Session. External Session close waits for submitters and joins
I/O/owner before releasing its C storage.

To restart, initialize a fresh nonzero instance after this shutdown. Call
operation/generation and packet identity counters exhaust instead of wrapping;
`WL_ERR_ID_EXHAUSTED` requires that same safe reconstruction. Stale handles and
completion tokens cannot be reused in the new instance. Fixed-width result
types and C/C++ storage boundaries are specified in the [API reference](kernel-api.md#types-and-abi).

## Read next

- [Kernel and packet-port API](kernel-api.md): signatures, errors and ownership.
- [RPC calls](tutorial-rpc.md), [concurrent calls](tutorial-rpc-async.md),
  [retained results](tutorial-rpc-values.md): application tasks.
- [Integration](tutorial-integration.md) and [adapters](adapters.md): owner loop
  and physical transport contracts.
- [`kernel.c` tests](../tests/core/kernel.c),
  [`packet_port.c` tests](../tests/core/packet_port.c) and
  [development commands](development-current.md): implementation and checks.
