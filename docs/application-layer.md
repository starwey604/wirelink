# Wirelink application-layer contract

Status: **design baseline for the typed routing, session, and RPC extensions**.

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
4. an optional session runtime supplies mailboxes, RPC correlation, and bulk
   transfer state.

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
from other threads enter a caller-supplied bounded command queue; only the
consumer invokes Wirelink. The queue may be SPSC when there is one client
producer. Multiple producers require an MPSC implementation, a critical
section, or an external executor chosen by the platform layer. The C core does
not acquire a hidden lock.

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

Every generated message descriptor selects one delivery policy:

| Policy | Storage and behavior | Intended use |
| --- | --- | --- |
| `DIRECT` | No retained message slot; run one callback in consumer context. | Commands consumed immediately. |
| `LATEST` | Decode/copy into one caller-supplied snapshot and increment a generation counter; a newer value coalesces an unread older value. | Control setpoints and telemetry where freshness wins over history. |
| `FIFO` | Decode/copy into a caller-supplied bounded ring; full policy and drop counter are explicit. | Events for which order and multiplicity matter. |
| `RPC` | Decode into or correlate with a fixed transaction/response slot. | Requests, responses, and application status. |

`LATEST` is deliberately not a core RX behavior. Wirelink still validates and
delivers accepted messages in order; coalescing occurs only after decode at the
application boundary. Cross-thread snapshots require a C11-atomic generation
protocol or a platform critical section. Volatile plus compiler barriers is
not a supported synchronization scheme.

Generated fixed arrays are copied into their inline destination arrays.
Borrowed variable-length fields cannot be retained by `LATEST`, `FIFO`, or
`RPC` unless the descriptor supplies explicit backing storage and a bounded
copy policy.

## 4. RPC and application completion

RPC is expressed by paired WLC messages and a generated service descriptor;
it is not encoded in the Wirelink header. Every RPC request and response has a
nonzero `uint32` operation ID. Zero is reserved for messages that are not
correlated operations. The operation ID is unique among the caller's active
slots for one peer context and is not reused until the old slot and any
response-cache entry have expired.

A service declaration fixes:

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
completion, but its result explicitly lacks link-delivery confirmation.

The server handler returns one of three dispositions:

- `COMPLETE`: send a response/status now;
- `PENDING`: retain only the operation metadata needed for a later explicit
  completion call; or
- `REJECTED`: send a declared application error without running the action.

Cancellation is best effort. It stops local waiting and may send a generated
cancel message, but cannot retract a request already delivered to the peer.
Long-running operations must define whether cancellation is supported and
what final response wins a cancel/complete race.

### 4.1 Duplicate requests and idempotency

Link-level retransmission does not redeliver a reliable DATA event within the
active Wirelink deduplication window. Application retries, reconnects, or lost
responses can nevertheless produce a second request with the same operation
ID. A server uses a caller-sized response cache:

- a completed cached operation replays its response without re-execution;
- an in-progress duplicate remains pending or receives a declared busy
  response; and
- a conflicting request that reuses an active ID is rejected.

After cache eviction or restart, exactly-once behavior is no longer assured.
Non-idempotent or safety-critical operations require a persistent product-level
operation key or must themselves be idempotent.

## 5. Allocation-free storage model

Initialization must calculate or validate every long-lived byte of storage:

- Wirelink core RX/TX/event storage;
- generated decode scratch and fixed-array storage;
- one slot per `LATEST` route;
- element size and capacity for every `FIFO` route;
- client RPC slots, server pending slots, and cached response bytes;
- cross-thread command-queue entries; and
- optional bulk-transfer state and chunk buffers.

No route may silently allocate when input exceeds its configured capacity.
Capacity exhaustion produces a typed error and counter. For a reliable
request, a router must not claim application acceptance merely because the
link already acknowledged storage; it returns a declared busy/error response
or applies the service's documented retry policy.

Future APIs should provide standalone application requirement structures and
query functions. They must not append fields to the frozen `wl_config_t`,
`wl_storage_t`, or `wl_event_t` structures.

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

## 8. Bulk transfer extension

Objects larger than `WL_FRAME_MAX_PAYLOAD`, and objects that should not be
assembled in RAM, use an application transfer extension. Increasing the core
constant or adding fragment fields to compact v1 is not the design path.

The initial transfer contract uses separately allocated message IDs for
`Begin`, `Chunk`, `End`, and `Abort`. It carries a nonzero transfer ID, total
object length, chunk offset, negotiated chunk size, and an object-level
CRC32C or stronger product hash. The receiver writes each accepted chunk to a
caller-provided `write(offset, span)` sink and commits only after final object
verification.

A correctness-first implementation may be sequential. A high-throughput
USB/UDP implementation can later add a bounded window and cumulative or
selective acknowledgement without changing the Wirelink frame header. Per-
frame `NONE` on USB/UDP does not remove the need for end-to-end object
verification across storage, restart, and resume.

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
