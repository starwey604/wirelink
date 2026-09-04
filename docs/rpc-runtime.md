# Allocation-free RPC runtime

`wirelink/rpc.h` correlates application requests and responses without adding
fields to the Wirelink v1 header. WLC-generated bindings still own the payload
schema: they encode/decode the nonzero `uint32_t` operation ID, application
status, request fingerprint, and service-specific payload.

The runtime owns no heap memory and has no hidden locks. Client slots, server
pending/cache slots, and bounded response byte storage are supplied by the
caller. All calls belong to the same single consumer that owns its
`wl_ctx_t`; generated handlers may route decoded responses into that consumer
but must not call a context recursively.

## Client lifecycle

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
must still call `wl_tx_take()` to release the terminal core transaction.

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

Auto-generated IDs advance monotonically and skip locally retained slots.
Callers that provide IDs explicitly must also avoid reuse while the peer may
still hold a response-cache entry.

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

When a point-to-point peer changes session, call
`wl_rpc_server_discard_session()` for the old nonzero session. It removes
pending and cached identities and reports in-flight handles through an optional
callback so the product can call `wl_tx_cancel()` and later `wl_tx_take()`.

Pending timeout and cache TTL are independently wrap-safe. Zero disables each
expiry. `wl_rpc_server_expired_acquire()` returns a timed-out pending request
token without discarding it; the application must reject/complete it with a typed
terminal response or explicitly abandon it. Eviction, expiry, session discard,
or process restart ends replay protection, so this is not durable exactly-once
execution; non-idempotent products need a persistent operation key or an
idempotent handler.
