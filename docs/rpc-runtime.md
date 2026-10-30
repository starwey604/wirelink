# Allocation-free RPC runtime

[Readiness and due-only expiry](execution-contract.md)

`wirelink/rpc.h` correlates requests and responses without changing the Wirelink
v1 frame header. New applications should use the generated managed RPC endpoint
described in the [RPC tutorial](tutorial-rpc.md). The following sections specify
its wire/ownership boundary, then the advanced low-level engine.

## Managed RPC and mapped interoperability

Request and response delivery independently default to reliable. Override a
binding with `@delivery(unreliable)`. Omitted defaults, explicit reliable
attributes and legacy reliable delivery properties produce identical code and
profile identities; duplicate policies for a direction are errors. Retained
LATEST/FIFO delivery remains explicit.

A profile which omits all three `request_operation_id`, `response_operation_id`,
and `response_status` mappings selects managed RPC. The `.wl` messages contain
business fields only. The generated runtime allocates correlation IDs, validates
metadata and response types, and prepares reply metadata automatically. Ordinary
codec output and schema identity remain independent of this policy.

Specifying all three mappings selects the existing mapped mode and preserves its
payload encoding and profile identity. Mapping names may differ between request
and response; the numeric values for a call must match. Partial mappings are a
compiler error. There is no automatic wire-format detection or fallback.

Managed RPC uses the unreleased `managed-rpc-v4-tagged-varuint32` format:

| Offset | Bytes | Content |
| --- | --- | --- |
| 0 | 8 | Nonzero originating client session, big-endian; echoed in every reply |
| 8 | 1–5 | Canonical unsigned LEB128 of `(operation_id << 1) | rejection`; the full nonzero 32-bit operation ID is preserved |

The native message ID selects request or response. The low bit of the correlation
value is zero for requests/success and one for a rejection. It does not duplicate
the request/response kind already known by dispatch. Normal metadata is 9–13 bytes.
A rejection adds one nonzero signed BE32 status immediately after the correlation
value, has no business body, and is 13–17 bytes. Request rejection bits, zero IDs,
noncanonical/overflowing correlation values and malformed rejection status/length
return `WL_RPC_ERR_MALFORMED_METADATA`. Metadata is not authentication. Reliable
request identity must match the link sender identity; replies with a different
local client identity report `WL_RPC_ERR_SESSION_MISMATCH` without changing calls.

The managed business body is tagged like the ordinary schema codec, with scalar
`fixed32` fields encoded as full-range `uint32` varints (wire type zero). The same
rule applies inside nested messages and to repeated scalar fields. Packed fixed32
arrays keep their fixed-width packed format. All other field encodings, field
numbers, presence, required checks and unknown-field skipping retain their codec
semantics. The generated runtime owns this private codec; ordinary `*_encode()` /
`*_decode()` APIs still emit/accept the frozen Schema-v1 TLV bytes. No application
integer range is narrowed. Canonical request fingerprints retain their ordinary
business-codec representation and are not computed from this transport encoding.

This change is unreleased: regenerate and rebuild both managed-RPC peers together.
There is one format, with no old-format fallback or negotiation. The managed wire
format contributes to binding-profile identity and is recorded separately in
runtime manifests. Mapped RPC, public business codecs and Compact-v1 framing are
unchanged. Merely replacing schema `= n` with `@id(n)` changes neither identity nor
bytes. Varint savings depend on values; maximum storage bounds cover the complete
integer domains rather than the benchmark's usual values.

## Ordinary asynchronous ownership

The ordinary entry is `<runtime>_endpoint.h` and `endpoint_<service>_async()`.
Owned requests are snapshotted before admission; completion prepares an owned
result and releases the call before notifying. Failed admission never notifies;
accepted calls notify once under continued driving or orderly close. Deadlines
are 1..2³¹−1 ms and include queue time. Optional `wl_rpc_call_t` enables cancellation;
there is no ordinary inspect/release. Immediate `config.on_<service>` handlers
return zero for success, nonzero for business rejection. Callback pointers expire
on return, but a struct copy is independent.

Managed defaults provide four slots and a finite recent-result cache.
`config.advanced` retains expert overrides; choose REJECT_NEW for strict retention.
See [platform waiting](rpc-platform.md) for synchronous calls and
[endpoint storage](endpoint-storage.md) for optional create/destroy. The manual
calls and deferred tokens below are explicit advanced choices. In particular,
their nonzero-handler-return rule does not apply to immediate handlers.

## Advanced manual calls and deferred reply ownership

Explicitly include `<runtime>_advanced.h` for these endpoint helpers.

Configure `wl_clock_t` once on the default endpoint. Call/complete/reject sample
it internally; replies during a step reuse that pass's time. There is no
pre-step requirement before a first or post-idle call. The advanced runtime
keeps explicit time; see [the clock boundary](endpoint-clock.md).

Managed endpoints provide service-specific `*_call_t`, `*_result_t`, and
`*_request_token_t` types. `endpoint_*_call()` returns a handle, `*_inspect()` returns
state and a typed response, and `*_release()` recycles terminal calls. A rejection
has `response_valid=false` and a nonzero `application_status`. `*_cancel()` cancels
local waiting and requests cancellation of a bound link TX; it does not undo remote
execution. Release failed/timed-out/cancelled calls too.

Endpoint call/complete/reject return `wl_rpc_err_t`, with detailed codec/link
diagnostics available through `endpoint_result()`. The advanced runtime functions
retain their tagged result. Replies prepared during a step preserve any earlier
failure of that pass, even when the reply itself succeeds.

Handles validate endpoint ownership, endpoint incarnation, service message IDs,
and client-slot generation. Stale or wrong-owner handles cannot release a new call.
Reply tokens validate the runtime, incarnation, and core server execution generation;
copy them for deferred work, without inspecting private members. Closing/reinitializing
a default endpoint invalidates both call handles and reply tokens. Custom runtime
users must discard tokens on reinit or maintain `rpc_incarnation` themselves.

Handlers return zero for locally accepted work, including deferred completion.
Use `*_complete()` for success or `*_reject(..., nonzero_status)` for business
failure. A nonzero handler return abandons locally and is a diagnostic, not an
automatic business rejection. Borrowed request fields expire at callback return.
Client responses require bounded schemas. RX decodes each successful body once
into its call's generated owned union member. Advanced `*_inspect()` returns a
`const *_value_t *` until release/close; assign `*response` to retain an independent
value. Async preparation copies the value into a separate completion union,
releases admission capacity, then notifies. Sync copies that owned completion.
Inspection, notification and sync do not decode retained bytes.

Concurrent replies are routed by call ID, not arrival order. Unknown/released-call
and already-terminal-call replies are ignored by the managed runtime with
`RUNTIME_OK`; `detail.rpc.rpc_result` preserves `NOT_FOUND` or `INVALID_STATE` for
an optional `on_result` observer. They do not fail another call. Malformed metadata,
wrong response types, and codec errors remain explicit dispatch errors.

Managed default capacity is compile-time configurable (four slots by default);
advanced runtime storage can be supplied independently.
ID-to-handle capture scans active slots, but later core handle lookup/cancel/release
is O(1). Core handles are scoped to one initialization lifetime. Generation tracking
fits the existing 64-byte client and slot storage; it creates no global counter,
heap, thread, or clock. Managed-only runtimes omit the old typed encoding scratch:
advanced manual requests encode into the TX claim. Ordinary asynchronous requests
first encode into a bounded request queue, then copy into the TX claim. Replies
encode into their reserved cache segment.
Static link/response capacities include the maximum 13-byte metadata and the
private managed body bound, and reserve at least 17 bytes for a rejection, including
empty response schemas. Public codec bounds remain unchanged; canonical-request
fingerprints cover only ordinary business codec bytes and are computed, not transmitted.

## Correlation is not business idempotency

Every managed `call()` starts a new operation. It does not expose an ID override
as a retry shortcut. The server suppresses duplicate wire requests and can replay
retained responses, within its configured session/cache/expiry scope. A new call
with the same business arguments may execute again. Durable or cross-retry
idempotency needs an explicit business key/state machine; it is not supplied by
this correlation mechanism.

Managed RPC matches the originating client session and call number. Endpoint
reconstruction obtains a fresh identity, isolating old-instance responses even
when a number is reused. Unreliable requests also carry identity and partition
the cache. Zero identity is malformed; a reliable request must match its link
sender. A response for another instance reports SESSION_MISMATCH diagnostically
without affecting other calls. No automatic old-format fallback is provided.
Identity is neither authentication nor an ordered version; this does not reject
arbitrary old requests or provide durable exactly-once execution. Mapped RPC has
no new identity field and retains its integration-owned freshness requirements.
See [automatic sessions](session.md) for source and exhaustion contracts.

## Low-level allocation and scheduling

The runtime owns no heap memory and has no hidden locks. Client slots, server
pending/cache slots, and bounded response byte storage are supplied by the
caller. All calls belong to the same single consumer that owns its
`wl_ctx_t`; generated handlers may route decoded responses into that consumer
but must not call a context recursively.

## Low-level client lifecycle

`wl_rpc_client_begin()` reserves a slot and starts its end-to-end deadline.
The deadline covers time in `QUEUED`, `LINK_PENDING`, and `WAIT_RESPONSE`, not
just response time. A timeout of zero disables deadline expiry. Every nonzero
timeout must be less than `2^31` milliseconds so unsigned subtraction remains
wrap-safe.

`wl_rpc_client_get_deadline_hint()` and
`wl_rpc_server_get_deadline_hint()` are side-effect-free. They return the
nearest relative application deadline (`0` when due and `UINT32_MAX` when no
deadline exists). The consumer can take the minimum of these values and the
core `wl_link_get_readiness()` deadline before sleeping; `poll()` and response preparation/commit enforce RPC
deadlines. These queries report timers only. Generated `*_runtime_readiness()`
also reports a queued server response as runnable when its delivery resources
are available, and blocked otherwise. Use that full readiness before waiting.

After encoding and sending a reliable request, bind its `wl_tx_handle_t` with
`wl_rpc_client_bind_tx()` and route its terminal TX event through
`wl_rpc_client_on_tx_event()`. `WL_EVT_TX_SUCCESS` enters `WAIT_RESPONSE` and
sets `link_delivery_confirmed` to one. It does not complete the RPC. The caller
of this low-level API must still call `wl_tx_take()` to release the terminal
core transaction. A WLC-generated runtime performs both operations when it
matches the terminal event; do not take that handle again.

For an unreliable request, or a send wrapper with no correlatable TX handle,
call `wl_rpc_client_tx_completed()` after local submission succeeds. It enters
`WAIT_RESPONSE` with `link_delivery_confirmed` zero. An exact response received
in `LINK_PENDING` is also accepted because application completion is stronger
evidence than ordering of a separate ACK/TX event. It completes the RPC while
retaining the TX handle and leaves `link_delivery_confirmed` zero. A response
in `QUEUED`, before any send has been bound or locally completed, is rejected.
The caller must independently cancel or drain/take the remaining core TX.
It must preserve or act on the retained handle before releasing the RPC slot.

`wl_rpc_client_response_prepare()` checks request/response message IDs,
operation, active state and deadline before granting generation-scoped authority
and the generated result index. Decode into that typed member, then call
`wl_rpc_client_response_commit()` with the same owner time. Commit checks the
authority and deadline again. A failed decoder leaves the call pending; no
partial value is published. Zero status publishes `COMPLETED` and
`response_valid`; nonzero status publishes `APPLICATION_ERROR` with no value.
Core client configuration contains slots/count/next ID, with no business buffer.

The generated storage descriptor supplies `rpc_results` (an actual array of
`*_rpc_result_value_t`) and `rpc_result_count` in addition to the byte arena.
Requirements report the required count separately. Initialization checks count,
alignment, multiplication overflow and overlap with the arena/instance before
changing the instance. The default descriptor and endpoint provide these typed
objects automatically. Client encoded response pools, `client_decode()` and
`decode_owned_response()` were removed in contract 0.10. Mapped responses decode
once into owned scratch to read their schema correlation fields, then copy into
the validated call. Server encoded pending/replay storage remains unchanged.

Cancellation only stops local waiting. Inspect the result before cancellation
to obtain any bound TX handle and optionally call `wl_tx_cancel()`; products
may also emit a generated cancel message. A late completion after cancellation
is rejected. Terminal slots and their operation IDs remain retained until
explicit release.

Likewise, an RPC deadline does not release a bound core transaction. On
`TIMED_OUT`, the caller should request `wl_tx_cancel()` if it is still active
and must eventually call `wl_tx_take()` after the core transaction reaches a
terminal state.

Auto-generated IDs advance monotonically and skip locally retained slots. A
mapped-mode generated client start uses a present nonzero request operation ID exactly;
absent or zero selects automatic allocation. After releasing a terminal client
slot, retrying the same canonical request with the same explicit ID can address
the server's bounded replay cache. Reusing that ID with different request data
is a conflict while the peer retains the cache entry.

## Per-method server guarantees

Omitted `guarantee`, or explicit `guarantee = replay;`, retains the duplicate
suppression and bounded history below. Explicit `guarantee = correlate;` admits
each valid arrival independently, including repeated session/operation IDs.
It does not compare canonical requests or retain completed RPC history. Neither
policy promises durable exactly-once execution or remote cancellation.

All methods share `rpc_server_pending_slot_count`. Replay uses
`rpc_server_cache_slot_count` / `rpc_server_response_capacity`; correlate uses
`rpc_server_correlate_slot_count` / `rpc_server_correlate_response_capacity`.
Only selected policy fields are generated. Mixed services calculate each
response bound separately. Defaults cover the complete method bounds; an
advanced caller's explicit encoded capacity remains its handler response limit.

Correlate reserves pending and stable response capacity before calling a handler.
Every execution has a distinct generation. Completion releases pending metadata,
but its response remains reserved through backpressure and acquired/inflight TX.
Unreliable sent or a reliable terminal releases it. Old tokens and responses cannot
complete or reclaim another execution. All-correlate generation omits canonical
helpers, history, and TTL paths. Report its performance as a different guarantee.

## Server duplicate and replay policy (default)

`wl_rpc_server_begin()` scopes operation IDs by the reliable sender's Wirelink
session. It compares all identity fields: peer session ID, operation ID,
request and response message IDs, and the caller-produced canonical-request
fingerprint. The fingerprint's collision quality is a product/schema
responsibility. A zero peer session denotes an unscoped request, as required
for transports or delivery modes that do not expose a reliable session.

- `NEW` atomically reserves pending metadata and one response-cache slot, then
  permits handler execution. If either pool is full, `begin()` returns a
  capacity error and the handler is not called.
- `PENDING_DUPLICATE` suppresses re-execution of an identical active request.
- `REPLAY` returns bounded cached response bytes for immediate re-encoding.
- `CONFLICT` reports reuse of the operation ID with any different identity in
  the same peer session. The same ID in a new peer session is `NEW`.

`complete()`, `reject()`, and `abandon()` take the exact
`wl_rpc_server_request_t` returned for the request. Its generation-stamped
identity permits simultaneous operations with the same numeric ID from
different peer sessions and prevents an old asynchronous completion from
targeting a reused pending slot. The token becomes invalid after any terminal
transition or session discard.

With `WL_RPC_CACHE_REJECT_NEW`, a full cache rejects the request before handler
execution. With `WL_RPC_CACHE_EVICT_OLDEST`, `begin()` may reserve the oldest
delivered generation; age comparison remains valid across counter wrap.
Entries awaiting submission or reliable completion are never eviction
candidates. Because response storage is already reserved, a valid completion
cannot later fail merely because another operation filled the cache.

## Server response ownership and progress

For generated responses, `wl_rpc_server_response_prepare()` borrows the cache
segment already reserved by `begin()`. Encode into that span and publish its
encoded prefix with `wl_rpc_server_response_commit()`; this avoids a second
response-sized scratch buffer and copy. A codec failure leaves the request
pending. The borrow ends on commit, reject, abandon, expiry, or session discard
and must never be written afterward. `complete()` and `reject()` remain copying
convenience APIs for callers whose bytes already live elsewhere.

Completion only makes owned bytes ready; it does not call the link. The owner
loop acquires one ready response with
`wl_rpc_server_response_acquire()` and then performs exactly one transition:

- synchronous backpressure: `wl_rpc_server_response_defer()`;
- accepted reliable TX: `wl_rpc_server_response_submitted()` with its handle;
- accepted unreliable TX: `wl_rpc_server_response_sent()`.

Route reliable terminal events through `wl_rpc_server_on_tx_event()`. Any
terminal result ends that bounded link attempt and leaves a delivered,
replayable entry. A duplicate request moves the same owned bytes back to the
ready queue; RPC does not create an unbounded retry loop above link ARQ.
Acquired bytes remain stable until their matching transition.

WLC-generated runtimes implement this sequence in `*_runtime_service()`, which
also advances client/server deadlines and submits at most one cached response
per call. Products should call it after event dispatch and application
completion, then combine its deadline result with `wl_link_get_readiness()`.

An ABI 18 WLC-generated server automatically observes the nonzero session on
each reliable RPC request. It performs only an inline equality check in the
steady state. On first binding or transition it calls the low-level observer,
discards old-session pending/cache state, requests cancellation of detached
responses, and sets `result.detail.rpc.peer_changed`. The application calls
`*_runtime_peer_observation_take()` once to revoke product leases and non-RPC
work. Call `*_runtime_peer_observe()` explicitly before reliable non-RPC
traffic that establishes the same product session.

Manual low-level RPC users instead zero-initialize `wl_rpc_peer_t` and pass
each observed session to `wl_rpc_peer_observe()` themselves, including the
cancellation callback. Multi-peer products can call
`wl_rpc_server_discard_session()` directly for each departed peer; the
generated single-peer tracker is not a routing or peer-table abstraction.

Pending timeout and cache TTL are independently wrap-safe. Zero disables each
expiry. `wl_rpc_server_expired_acquire()` returns a timed-out pending request
token without discarding it; the application must reject/complete it with a typed
terminal response or explicitly abandon it. Eviction, expiry, session discard,
or process restart ends replay protection, so this is not durable exactly-once
execution; non-idempotent products need a persistent operation key or an
idempotent handler.

## Small instruction-cache builds

The ESP32-S3 cross benchmark uses the default 16 KiB ICache, 240 MHz,
shared CRC16/COBS, ARQ off, and one call in flight. The optimized cohort applies
`-Os` and application LTO equally to Wirelink, eRPC and CSP. Large RPC dispatch
and frame parsing retain explicit function boundaries; globally suppressing
inlining regressed this workload. These settings do not enlarge ICache or move
RPC code to IRAM. Keep compiler profiles separate from the historical `-O2`
cohort when comparing results.

The measured residual subtracts each call's actual UART TX+RX occupancy before
computing percentiles. It includes critical-path software, scheduling and cache
waits, and is not exclusive CPU time or a hard-real-time bound for other schemas
and workloads. Both peers must regenerate the same managed RPC wire format.
