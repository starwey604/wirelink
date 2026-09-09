# Default endpoint: design and boundaries

Internal development, codegen ABI 30. No release or main merge. Managed RPC uses
metadata v2 and requires paired upgrades; Compact-v1 framing and mapped payloads stay unchanged.
Read [installation](installation.md), [telemetry](getting-started.md),
[RPC](tutorial-rpc.md), then [integration](tutorial-integration.md).
[中文](default-endpoint-cn.md).

## Ordinary application entry

WLC emits `<runtime>_endpoint.h` for bounded one-frame profiles. Zero-initialize a
stable `*_endpoint_t`, supply a platform environment, attach an adapter and use
send/read, asynchronous RPC and step. Never modify `private_state`.
Initialization generates the identity automatically. The default is
`wl_platform_environment()`; custom sources are described in [automatic sessions](session.md).

The header transitively includes runtime declarations to support static C layout;
it is the recommended entry, not an opaque ABI hiding every declaration.
`<codec>_values.h` is the independent owned-business-data entry. Manual endpoint
call/inspect/release and complete/reject helpers require `<runtime>_advanced.h`;
the ordinary entry does not include them. Advanced views and custom assembly
remain codec/runtime mechanisms.

The generic endpoint owns the clock sample, adapter attachment and owner pass;
generated glue supplies storage and typed business conversion; the adapter handles
input, TX completion, wakeup and quiescence. Applications provide business logic
and scheduling. Core owns no heap, thread or OS clock.

## Ordinary RPC contract

Deployment profiles may select a fixed envelope and local RPC role to trim
storage: [endpoint layout configuration](endpoint-layout.md). Omitted layout
properties retain `any` / `both`; client-only configs omit server handlers, and
server-only ordinary headers omit client call helpers.

`endpoint_<service>_async(endpoint, request, timeout, callback, context, optional_call)`
snapshots the request before acceptance. WL_OK means accepted; WL_ERR_BUSY means
local bounded capacity is full. Failed admission never calls back. Timeouts must
be 1..2³¹−1 ms and include queue waiting; retries do not restart them.

Completion first decodes an owned value and releases the RPC call, then invokes
the callback. Independent link TX leases/terminal events remain the owner's job.
Callbacks receive per-call `wl_rpc_completion_t` diagnostics and a typed response
only on success. Pointers are callback-scoped; copying `*response` retains an
independent value with no release/destructor. Bounded string/bytes contain length
and inline data arrays; string length is in bytes.

Register `config.on_<service>` for an immediate handler taking const request and
mutable response. Return zero for success, nonzero solely for business rejection.
Ordinary handlers and diagnostics share `config.user_data`. A non-NULL
`config.<service>_user_data` overrides it for one service; NULL inherits.
These are business contexts, not reply machinery. Deferred handlers and per-call
completions retain explicit contexts and do not inherit the shared pointer.
Long-running work explicitly selects the advanced deferred-token handler under
`config.advanced.<service>_request_handler`; registering both forms is an error.

## Growing a service collection

Keep RPC declarations in one shared profile and combine them with endpoint-local
routes using CMake `PROFILES` or repeated WLC `--profile`. Order does not override
definitions: duplicates and conflicts fail generation. A `send Message` binding
generates sending and contributes to payload bounds without a receive mailbox;
the receiving endpoint independently selects `latest` or `fifo`.

The [12-service example](../examples/03_device_service/README.md) shows module-local
handler registration and tests adding a thirteenth service without editing the
transport, owner loop, build rules or other handlers. ABI 27 leaves ABI 26 codec
and wire bytes unchanged; generated consumers must be rebuilt with matching WLC.

## Defaults and RAM

Client capability is prepared at init; registered handlers enable server capability.
Ordinary code needs no role-enable calls. Defaults are native packet, CRC32C,
100 ms ACK timeout, four retries and 16 events per owner pass. These are adjustable
starting points, not universal deployment settings.

Managed endpoints have four client/pending/cache slots, a 1000 ms pending timeout
and 10000 ms cache TTL; FIFO remains one slot. EVICT_OLDEST evicts only delivered
responses and protects pending/ready/in-flight work. TTL is a maximum age, not a
guaranteed retention window. Strict retention uses
`config.advanced.rpc_server_cache_policy = WL_RPC_CACHE_REJECT_NEW`.
Resource pressure is diagnosed locally by the server; the client retains its
original deadline. No new remote BUSY frame or fabricated rejection is introduced.

Set `<PREFIX>_ENDPOINT_RPC_CAPACITY=1` at build time to reduce static capacity,
consistently across every translation unit using that endpoint. Runtime counts
cannot exceed it. `config.advanced` and `config.link` are expert overrides.
Queues are bounded and reliable transactions remain single-window; waiting for
an ACK no longer reserves the physical DATA transmitter exclusively.

Only selected messages contribute to storage; managed metadata adds 20 bytes.
Request queues use the largest request bound, not the largest response bound.
Services share a largest-request/response scratch union and bounded runtime decode
scratch across services. ABI 29 computes canonical fingerprints without a separate
canonical-request byte buffer. Dispatch must not reenter; deferred work copies
inputs and tokens, not scratch pointers. Unbounded profiles retain per-service
decode objects for caller-configured repeated backing. Near-2-KiB responses
substantially enlarge endpoints; see the [implementation record](rpc-usability-progress-cn.md).
Unbounded/oversized selections emit HAS_DEFAULT_ENDPOINT=0 rather than silently
inventing storage. Constrain the schema or choose advanced assembly.

## Driving, diagnostics and close

Ordinary `endpoint_<service>_sync()` drives and waits on the owning thread, or
uses a bound host executor proxy. UDP installs waiting automatically; custom
platforms supply a waiter once. Missing waiting is an explicit error, not a busy
loop. See [platform integration](rpc-platform.md) for deadlines, thread-safe
proxy clocks, stop notification and lifetimes. Async remains the event-loop entry.

Step samples the clock once; callback submissions reuse that sample.
Advanced link/runtime calls still take explicit time. Never run two owners.
A successful step means normal progress, not RPC success.
`endpoint_result()` retains the pass's first runtime error; generic pass detail
is available via `wl_endpoint_last_step(endpoint_handle(...))`.
`config.on_result` receives diagnostics, independently of per-call completion.
Cached replies retain their reservation and retry ordinary TX backpressure.

Callbacks may submit/cancel calls, but cannot recursively step, synchronously
close or reinitialize their own endpoint. Close from an owner safe point:
generated close quiesces the adapter and notifies remaining calls before returning.
It is idempotent and permits subsequent reinit. Do not move a live endpoint.
Calling generic `wl_endpoint_close(handle)` directly skips generated notifications;
ordinary applications must call generated close before freeing adapter resources.

Closing either loopback endpoint stops the cable; close both before releasing it.
Drivers privately bound outside attach still need explicit shutdown by their owner.
Return advanced borrowed views first. Cancellation/timeout never undoes remote
side effects; a finite replay cache cannot promise perpetual exactly-once behavior.

## Advanced paths and validation

Manual call/inspect/release, deferred tokens and value/view conversion are advanced
opt-ins. Do not manually release a callback-managed call. Custom arenas,
zero-copy large data and manual dispatch belong to advanced assembly.
LATEST/FIFO still require retainable pointer-free messages; this stage does not
expand IDL or introduce streaming.

Ordinary `endpoint_<service>_sync()` drives/waits on its owner thread, or submits
through a bound host executor. UDP installs waiting automatically; custom platforms
provide a waiter during initialization. A missing waiter is an error, not a busy
loop. Async remains the event-loop entry. See [platform integration](rpc-platform.md)
for clocks, stopping and lifetimes.

See the [implementation record](rpc-usability-progress-cn.md) for H1 evidence
and M3/M4 software gates. Optional [allocator creation](endpoint-storage.md) retains
the static execution path; software checks and H2 hardware validation passed.
M5 remains pending.
