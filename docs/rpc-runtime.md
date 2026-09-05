# Allocation-free RPC runtime

`wirelink/rpc.h` correlates requests and responses without changing the Wirelink
v1 frame header. New applications should use the generated managed RPC endpoint
described in the [RPC tutorial](tutorial-rpc.md). The following sections specify
its wire/ownership boundary, then the advanced low-level engine.

## Managed RPC and mapped interoperability (codegen ABI 20)

A profile which omits all three `request_operation_id`, `response_operation_id`,
and `response_status` mappings selects managed RPC. The `.wl` messages contain
business fields only. The generated runtime allocates correlation IDs, validates
metadata and response types, and prepares reply metadata automatically. Ordinary
codec output and schema identity remain independent of this policy.

Specifying all three mappings selects the existing mapped mode and preserves its
payload encoding and profile identity. Mapping names may differ between request
and response; the numeric values for a call must match. Partial mappings are a
compiler error. There is no automatic wire-format detection or fallback.

Managed RPC adds this fixed 12-byte prefix before the ordinary business codec body:

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 1 | `0x00`, invalid as a legacy codec field tag |
| 1 | 1 | Metadata version `1` |
| 2 | 1 | Kind: request `1`, response `2` |
| 3 | 1 | Reserved, must be zero |
| 4 | 4 | Nonzero correlation ID, unsigned big-endian |
| 8 | 4 | Signed 32-bit business status, two's-complement big-endian |

Requests carry status zero. Successful responses carry status zero and the encoded
response body. Rejections carry nonzero status and **no body**, even if the response
schema has required fields. Bad prefixes, zero IDs, nonzero request status, or a
rejection with a body return `WL_RPC_ERR_MALFORMED_METADATA`. A successful body is
still validated by its ordinary codec. Metadata is not authentication.

Both peers must select the same mode. Switching an existing service from mapped
to managed changes its payload bytes even if its DATA message IDs stay the same;
upgrade both peers together or allocate distinct message IDs. The managed mode
and metadata revision contribute to the binding-profile identity. Merely replacing
schema `= n` with `@id(n)` changes neither identity nor bytes.

## Default call and reply ownership

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
Use `*_complete()` for success or `*_reject(..., nonzero_status, now)` for business
failure. A nonzero handler return abandons locally and is a diagnostic, not an
automatic business rejection. Borrowed request fields expire at callback return.
Borrowed fields in a decoded response, where the schema permits them, expire at
call release; copy their contents if they must live longer.

Concurrent replies are routed by call ID, not arrival order. Unknown/released-call
and already-terminal-call replies are ignored by the managed runtime with
`RUNTIME_OK`; `detail.rpc.rpc_result` preserves `NOT_FOUND` or `INVALID_STATE` for
an optional `on_result` observer. They do not fail another call. Malformed metadata,
wrong response types, and codec errors remain explicit dispatch errors.

The default endpoint has one client slot; custom runtime storage enables more.
ID-to-handle capture scans active slots, but later core handle lookup/cancel/release
is O(1). Core handles are scoped to one initialization lifetime. Generation tracking
fits the existing 64-byte client and slot storage; it creates no global counter,
heap, thread, or clock. Managed-only runtimes omit the old typed encoding scratch:
requests encode into the link TX claim, replies into their reserved cache segment.
Static link/response capacities include the 12-byte prefix; canonical-request
fingerprints cover only business codec bytes and are computed, not transmitted.

## Correlation is not business idempotency

Every managed `call()` starts a new operation. It does not expose an ID override
as a retry shortcut. The server suppresses duplicate wire requests and can replay
retained responses, within its configured session/cache/expiry scope. A new call
with the same business arguments may execute again. Durable or cross-retry
idempotency needs an explicit business key/state machine; it is not supplied by
this correlation mechanism.

Local handle generations do not add a wire incarnation. A reused numeric call ID
cannot distinguish arbitrarily delayed old responses. Drain/reset the transport
when replacing a client instance, avoid wire-ID reuse while old replies can remain,
and do not claim this format provides cross-reboot response freshness. Reliable
server peer observation scopes request replay, not durable exactly-once execution
or client-side freshness across restarts. Unreliable requests have no such reliable
peer-session scope. These limitations also apply to the mapped path.

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
core `wl_poll_get_hint()` deadline before sleeping; only `poll()` advances RPC
deadlines. A ready server response reports deadline zero so the owner services
it before sleeping.

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

Responses require an exact operation ID and response message ID. Bytes are
copied into that slot's fixed response segment and remain valid until
`wl_rpc_client_release()`. A zero application status enters `COMPLETED`; a
nonzero service status enters `APPLICATION_ERROR`. Oversize responses also
enter `APPLICATION_ERROR`, with `runtime_error` set to
`WL_RPC_ERR_RESPONSE_TOO_LARGE`.

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

## Server duplicate and replay policy

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
completion, then combine its deadline result with `wl_poll_get_hint()`.

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
