# Wirelink application-layer contract

Status: **implemented baseline for typed routing, `LATEST`, `FIFO`, RPC,
sequential bulk transfer, and the optional C++20 host executor**. Embedded task
and queue integration remains platform-owned.

This document fixes the boundary between the frozen Wirelink v1 link protocol
and the allocation-free application facilities built above it. It is not a
new Wirelink frame format. Typed dispatch, delivery policy, RPC correlation,
and bulk transfer remain payload-layer facilities and must not add fields to
the compact v1 header.

## 1. Layering and non-goals

The application stack has four independent layers:

1. a platform adapter moves complete transmission units;
2. the Wirelink core frames DATA, performs link retry and deduplication, and
   exposes a borrowed `wl_event_t`;
3. WLC-generated bindings decode and route the application payload; and
4. optional application runtimes supply `LATEST` mailboxes, ordered `FIFO`
   queues, RPC correlation, and sequential bulk-transfer state.

A Wirelink ACK proves only that a valid reliable DATA packet reached stable
event storage at the peer. For reliable DATA, `WL_EVT_TX_SUCCESS` therefore
means **link delivery**; for unreliable DATA it means only successful local
transmission. Neither means successful command execution. An application
operation is complete only after its declared response or status message has
been decoded.

The application layer does not provide durable exactly-once execution,
authentication, peer discovery, multi-peer routing, or transparent sharing of
one context by several transports. Products may add those policies above the
interfaces defined here.

## 2. Context and execution ownership

One `wl_ctx_t` represents one configured link profile and one logical peer.
Each physical link or peer has an independent context, RX storage, TX storage,
session ID, router, and application state. Mirroring a message over two links
is explicit application fan-out; bytes received by two adapters must not be
fed into the same context.

The existing core ownership rules remain unchanged:

- exactly one producer owns RX feed, reserve, DMA, or unit-queue publication;
- exactly one consumer owns adapter service, sends, `wl_poll()`, transaction
  queries, and `wl_event_release()`; and
- adapter callbacks and ISRs publish bytes or completion state and wake the
  consumer, but never run generated decoders or application handlers.

A threaded session runtime uses a dedicated consumer task or executor. Calls
from other threads enter a bounded command queue; only the consumer invokes
Wirelink. The optional `Wirelink::host` C++20 target implements this pattern
with an owner thread, SPSC RX feed, wake semaphore, and fixed coalescing
outbox. Embedded products provide the corresponding RTOS task and queue. The C
core does not acquire a hidden lock.

The consumer drains work in this order:

1. consume adapter RX/TX completions;
2. poll and route all immediately available Wirelink events;
3. advance application RPC and transfer deadlines;
4. submit bounded queued commands; and
5. wait until adapter activity, command-queue activity, or the earliest core
   or application deadline.

## 3. Typed dispatch and borrowed lifetime

Generated dispatch maps a permanent `message_id` to exactly one decoder and
handler descriptor. Unknown IDs, codec failures, and handler failures are
different results and have independent counters.

The default dispatcher uses callback-scoped borrowing:

```text
wl_poll -> decode borrowed payload -> invoke typed handler -> release event
```

The dispatcher, not the handler, calls `wl_event_release()` exactly once after
the handler returns. Borrowed `bytes` and `string` fields, and all pointers into
the undecoded payload, cease to be valid at that point. A handler must copy or
move the required data into caller-owned storage before returning. A handler
must not recursively call the same router or Wirelink context.

The schema does not bake scheduling or ownership policy into its wire format.
The application wires each typed route to one delivery policy:

| Policy | Storage and behavior | Intended use |
| --- | --- | --- |
| `DIRECT` | Run the generated typed callback in consumer context without retaining the decoded value. | Commands consumed immediately. |
| `LATEST` | Decode directly into a caller-supplied three-slot SPSC mailbox claim; a newer value coalesces an unread older value. | Control setpoints and telemetry where freshness wins over history. |
| `FIFO` | Decode directly into a caller-supplied bounded SPSC ring; full rejects the new value and increments an explicit counter. | Events for which order and multiplicity matter. |
| `RPC` | Route decoded operation metadata into fixed client/server slots. | Requests, responses, and application status. |

`LATEST` is deliberately not a core RX behavior. Wirelink still validates and
delivers accepted messages in order; coalescing occurs only after decode at the
application boundary. `wirelink/latest.h` uses a lock-free C11-atomic
front/middle/back ownership exchange so a non-atomic typed value can be read
without tearing. It reports `WL_ERR_NOT_SUPPORTED` when the required atomics
are not lock-free. Volatile plus compiler barriers is not a supported
synchronization scheme.

`wirelink/fifo.h` uses release-published and release-reclaimed wrapped cursors
over caller-owned fixed-size slots. Its consumer borrows the oldest value until
explicit release, so the producer cannot overwrite either unread or borrowed
data. A full queue rejects the new value; applications that prefer coalescing
must select `LATEST` instead.

Generated fixed arrays are copied into their inline destination arrays.
Borrowed variable-length fields cannot be retained by `LATEST`, `FIFO`, or
`RPC` unless the descriptor supplies explicit backing storage and a bounded
copy policy.

## 4. RPC and application completion

RPC is expressed by paired WLC messages and explicit application/runtime
configuration; it is not encoded in the Wirelink header. Every RPC request and
response carries a nonzero `uint32` operation ID. Zero is reserved for messages
that are not correlated operations. The operation ID is unique among the
caller's retained slots for one peer context and is not reused while the peer
may still retain its response-cache entry.

A service binding fixes:

- request and response message IDs;
- whether the request and response use reliable or unreliable DATA;
- the response status domain and optional result payload;
- the local deadline and cancellation policy; and
- whether the handler is synchronous, asynchronous, or explicitly
  idempotent.

Link and application state remain separate:

```text
FREE -> QUEUED -> LINK_PENDING -> WAIT_RESPONSE -> COMPLETED
                    |                 |               |
                    +-> LINK_FAILED   +-> TIMED_OUT   +-> APPLICATION_ERROR
                                      +-> CANCELLED
```

`LINK_PENDING` finishes when the Wirelink TX transaction finishes. A link
success advances to `WAIT_RESPONSE`; it never manufactures an application
success. An unreliable request may enter `WAIT_RESPONSE` after local TX
completion, but its result explicitly lacks link-delivery confirmation. An
exact application response may arrive while the reliable request is still
`LINK_PENDING`; it is accepted as stronger evidence of execution, completes
the RPC with link confirmation unset, and leaves the independent core TX
handle for the caller to cancel/drain and finally `wl_tx_take()`.

`wl_rpc_server_begin()` classifies a request before the application handler
runs:

- `NEW` reserves bounded pending metadata and response storage before
  permitting one execution;
- `PENDING_DUPLICATE` suppresses re-execution;
- `REPLAY` returns the cached response bytes; and
- `CONFLICT` reports reuse of an operation ID by a different request identity
  within the same reliable peer session.

The application can complete or reject a `NEW` operation immediately, retain
its generation-stamped request token for later asynchronous completion, or
explicitly abandon it. Capacity failure occurs before the handler runs; after
`NEW`, the corresponding response slot is already reserved.

Cancellation is best effort. It stops local waiting and may send a generated
cancel message, but cannot retract a request already delivered to the peer.
Long-running operations must define whether cancellation is supported and
what final response wins a cancel/complete race.

### 4.1 Duplicate requests and idempotency

Link-level retransmission does not redeliver a reliable DATA event within the
active Wirelink deduplication window. Application retries, reconnects, or lost
responses can nevertheless produce a second request with the same operation
ID. A server uses a caller-sized response cache keyed by the reliable sender
session and application operation identity:

- a completed cached operation replays its response without re-execution;
- an in-progress duplicate remains pending or receives a declared busy
  response; and
- a conflicting request that reuses an active ID within one peer session is
  rejected, while the same ID from a new session is an independent request.

After cache eviction or restart, exactly-once behavior is no longer assured.
Non-idempotent or safety-critical operations require a persistent product-level
operation key or must themselves be idempotent.

ABI 18 generated runtimes automatically observe the peer session before a
reliable RPC request handler runs. A transition discards the old session's
pending/cache state and detached responses; `*_peer_observation_take()` lets
the product revoke its own leases. Reliable non-RPC traffic that establishes
the same authority boundary must call generated `*_runtime_peer_observe()`
explicitly. Multi-peer routing still requires one context/runtime per peer or
a product-owned peer table around the low-level RPC engine.

## 5. Allocation-free storage model

Initialization must calculate or validate every long-lived byte of storage:

- Wirelink core RX/TX/event storage;
- generated decode scratch and fixed-array storage;
- one slot per `LATEST` route;
- element size and capacity for every `FIFO` route;
- client RPC slots, server pending slots, and cached response bytes;
- cross-thread command-queue entries; and
- bulk sender/receiver state and caller-owned repeatable source or sink
  storage.

No route may silently allocate when input exceeds its configured capacity.
Capacity exhaustion produces a typed error and counter. For a reliable
request, a router must not claim application acceptance merely because the
link already acknowledged storage; it returns a declared busy/error response
or applies the service's documented retry policy.

Application facilities use standalone configuration/storage structures and,
where computed alignment is needed, requirement queries. The bulk runtime uses
its own fixed-size opaque sender and receiver contexts. Future command-queue
and session APIs must follow the same pattern and must not append fields to the
frozen `wl_config_t`, `wl_storage_t`, or `wl_event_t` structures.

For WLC-generated retained/RPC profiles, the assembly API supplies mechanical
defaults, role enable helpers, exact requirements, and a default aligned arena
when every payload is statically bounded. `*_runtime_init_checked()` is the
bring-up path for field/capacity diagnostics; ordinary `*_runtime_init()` keeps
the firmware path smaller once configuration is proven.

## 6. Error and health domains

A session-facing error preserves its source domain:

| Domain | Examples |
| --- | --- |
| Transport | disconnect, endpoint stall, socket or device I/O failure |
| Link | retry exhaustion, cancellation, sink failure, malformed or unsupported frame |
| Codec | malformed field, wrong wire type, invalid UTF-8, array length or capacity error |
| Routing | unknown message ID, missing handler, route FIFO full |
| RPC | no free slot, response mismatch, deadline, duplicate/conflicting operation ID |
| Application | declared command rejection or product-specific failure status |

Flattening all domains to timeout or generic I/O failure is not conforming
session behavior. A health snapshot should combine last-valid-RX time,
transport connection state, core counter deltas, route drops, RPC outcomes,
and application errors without treating ordinary unreliable loss as a
disconnect.

The optional `Wirelink::diagnostics` target formats these existing snapshots
into caller-owned key/value text. It does not add a registry, logging backend,
clock, allocation, or counters of its own.

## 7. Multiplexing and transport selection

`COBS_STREAM` is self-resynchronizing only with respect to its `0x00`
delimiter. Its first encoded byte is a COBS code and is not a stable protocol
marker. A receiver must not distinguish Wirelink from another byte-stream
protocol by inspecting the first byte or by feeding the same bytes to several
stateful parsers until one accepts them.

Sharing a physical stream therefore requires one explicit mechanism:

- separate USB interfaces/endpoints or serial channels;
- an out-of-band mode switch while every parser is quiescent; or
- an outer multiplexer with an unambiguous channel ID and length before the
  inner Wirelink transmission unit.

The outer multiplexer owns resynchronization between channels. Its header and
integrity policy are outside the Wirelink v1 header.

## 8. Sequential bulk transfer extension

Objects larger than `WL_FRAME_MAX_PAYLOAD`, and objects that should not be
assembled in RAM, use an application transfer extension. Increasing the core
constant or adding fragment fields to compact v1 is not the design path. The
extension is a standalone allocation-free state machine and does not append
fields to `wl_config_t`, `wl_storage_t`, or `wl_event_t`.

The first contract is single-peer, single-active-transfer, upload-direction,
and strictly sequential:

```text
Begin -> Chunk x N -> End
           ^          |
           +-- Status-+
Abort -------- Status
```

Five separately allocated application message IDs represent `Begin`,
`Chunk`, `End`, `Abort`, and `Status`. Their semantic fields are:

| Message | Required fields |
| --- | --- |
| `Begin` | nonzero `transfer_id`, `total_length`, requested chunk size, object CRC32C |
| `Chunk` | `transfer_id`, absolute byte `offset`, borrowed `bytes` |
| `End` | `transfer_id`, repeated total length and object CRC32C |
| `Abort` | `transfer_id`, application reason |
| `Status` | `transfer_id`, acknowledged phase, result, cumulative `next_offset`, accepted chunk size |

`wl_bulk_sender_action_acquire()` lends the next action to the application.
After encoding and local TX acceptance, the application calls
`wl_bulk_sender_action_submitted()`; transport backpressure instead uses
`wl_bulk_sender_action_defer()`. The generated receive route synchronously
calls the matching `wl_bulk_receiver_on_*()` function, then acquires its
retained Status. Status is released only after local TX acceptance, or deferred
for a later attempt. Sender and receiver `poll()` calls advance wrap-safe
timeouts, and their deadline hints participate in the consumer loop's earliest
wake deadline.

`Status.next_offset` is the only application-level cumulative acknowledgement.
A Wirelink ACK is produced before typed decode and persistent sink completion,
so link success must never be interpreted as successful object storage or
commit. A receiver emits `Status` only after the corresponding synchronous
sink operation has returned. A lost `Status` is recovered by repeating the
same application message; duplicate `Begin`, already-consumed `Chunk`, and
terminal `End` must not repeat sink side effects.

The receiver negotiates a chunk size no larger than its configured maximum and
asks the caller-provided sink for a valid resume offset. It accepts only the
exact next chunk. A chunk entirely below `next_offset` is an already-consumed
duplicate and is acknowledged without writing it again; gaps, partial
overlaps, integer overflow, and bytes beyond `total_length` are rejected. The
final short chunk is allowed, while every earlier chunk and offset obey the
declared sink alignment.

Chunk bytes remain borrowed from the decoded `wl_event_t` and must be consumed
synchronously by `write(offset, span)`. `BUSY` means that the sink consumed no
bytes and changed no durable state. The transfer runtime never retains the
span, never allocates an object-sized buffer, and owns only constant-size
descriptor, offset, deadline, retry, status, and statistics state. A sender
likewise exposes actions containing offsets and lengths; the application reads
repeatable source bytes only while encoding that action into the final
Wirelink TX claim.

`End` calls the sink's final verification/commit exactly once. Verification
must cover the bytes actually stored, including a resumed prefix. CRC32C is
the interoperability baseline, but a product may verify a stronger digest or
signature before returning success. This object-level check is distinct from
per-frame integrity: the v1 USB vendor-bulk and UDP profiles remain
`COBS_STREAM + WL_INTEGRITY_NONE`.

Cancellation is retained as a transfer-ID tombstone even when `Abort` arrives
before its delayed `Begin`. A later `Begin` for that same ID is rejected, while
a different ID may replace the tombstone after its Status has been released.
Replacing a failed receiver session first calls the old sink's `abort`, so the
runtime never transfers sink ownership by merely overwriting its descriptor.
If the replacement sink returns `BUSY`, the old terminal record remains in
place and continues to reject delayed traffic from that transfer.

The initial sender keeps one application message outstanding and retries on a
bounded, wrap-safe Status deadline. `BUSY` uses a nonzero retry delay rather
than a zero-delay poll loop. USB/UDP profiles should normally send `Chunk` and
`Status` as unreliable DATA and use this application acknowledgement, avoiding
two stop-and-wait layers. A serial profile may select reliable DATA, but still
waits for `Status` before declaring sink success.

Sender reset is restricted to idle, completed, aborted, or failed-Abort states.
An active sender or a sender that failed in `Begin`, `Chunk`, or `End` must
request `Abort` and run its bounded retry policy before reuse. Reset after the
Abort retries themselves fail is an explicit force-abandon operation; products
that need eventual autonomous peer recovery must configure a nonzero receiver
idle timeout.

Receiver reset requires that no Status view is acquired. It aborts any active
sink and explicitly clears terminal history, including an Abort tombstone; it
is therefore a local lifecycle boundary rather than a peer-visible cancel.

`transfer_id` values must not be reused while the peer may still retain their
terminal record. Reusing an ID can only replay its previous idempotent result;
use a fresh nonzero ID for each logical object transfer.

The first implementation deliberately excludes multi-chunk windows, out-of-
order reassembly, object-sized RAM staging, asynchronous retention of a Chunk
span, download direction, and resume state that survives process or MCU
restart. A later bounded window or persistent checkpoint format may extend the
application messages without changing the Wirelink v1 frame header.

The additive message definitions and generated C fixture live in
[`tests/fixtures/wlc`](../tests/fixtures/wlc). Unit state-machine coverage is in
[`bulk_sender`](../tests/zephyr/unit/bulk_sender) and
[`bulk_receiver`](../tests/zephyr/unit/bulk_receiver); the generated-message,
two-context 1 MiB path is in
[`bulk_transfer`](../tests/zephyr/integration/bulk_transfer). The reproducible
CPU/goodput method and initial host record are in
[`bulk-performance.md`](bulk-performance.md). The generated-message 1 MiB
ESP32-S3/Astrial path, including USB and device-CPU attribution, is recorded in
[`usb-performance.md`](usb-performance.md#2026-09-01-sequential-1-mib-object-transfer).

## 9. Required verification

The application extensions are not complete until tests cover:

- exact generated vectors and cross-version WLC compatibility;
- callback return followed by event release, including borrowed-field misuse
  checks under sanitizers;
- `LATEST` coalescing, generation wrap, and concurrent consistent reads;
- FIFO full behavior without overwriting a borrowed or unread element;
- link ACK success followed by application rejection;
- lost response, duplicate request, cached replay, conflicting ID, timeout,
  cancellation, and asynchronous completion;
- command-queue saturation and consumer wake/deadline races;
- two peers using independent contexts and session state;
- explicit multiplexing recovery after a malformed inner unit;
- transfer interruption, duplicate chunks, resume, final-integrity failure,
  and bounded RAM use; and
- host, `native_sim`, QEMU, sanitizer, stack, and representative hardware
  performance gates.

The link conformance vectors must remain byte-for-byte unchanged throughout
this work.
