# Wirelink 1.0 API Boundary

Status: pre-1.0 design contract. Compact-v1 wire bytes are frozen; the C and
generated-runtime surfaces may still be corrected before the first 1.0
release. This document is the API map to review, not a list of every prototype.

## Suggested Review Order

New to Wirelink? First read the application-led tutorials:
[latest temperature](getting-started.md), [RPC calculation](tutorial-rpc.md),
then [project and hardware integration](tutorial-integration.md).
The order below is for reviewing the API after using those examples.

1. Read this document through **Pre-1.0 Review Points** to decide whether the
   library boundary and ownership model are acceptable.
2. Compare the tutorials with compiled [`latest_telemetry.c`](../examples/latest_telemetry.c)
   and [`getting_started.c`](../examples/getting_started.c).
3. Review [`adapters.md`](adapters.md) beside
   [`application-layer.md`](application-layer.md) to check the producer,
   consumer, pump, and shutdown split.
4. Review the [WLC guide](https://github.com/starwey604/wlc/blob/6c992decc4b200d258bd8c7409a8896ab37a17e8/README.md) and
   [`schema-v1.md`](schema-v1.md), then inspect one representative generated
   [`control_runtime.h`](../tests/fixtures/wlc/generated/current/control_runtime.h).
5. Inspect only the application policies you intend to expose:
   [`latest-mailbox.md`](latest-mailbox.md), [`fifo.md`](fifo.md),
   [`rpc-runtime.md`](rpc-runtime.md), and
   [`bulk-performance.md`](bulk-performance.md).
6. Finish with [`compatibility.md`](compatibility.md) and the normative
   [`protocol.md`](protocol.md). Read [`api-v1-audit.md`](api-v1-audit.md) only
   as historical evidence for earlier decisions.

## Boundary in One Sentence

Wirelink is an allocation-free, owner-driven, point-to-point link plus optional
typed application runtimes. It owns framing, integrity, link acknowledgement,
retry, duplicate suppression, and borrowed events. It does not own a driver,
thread, heap, clock, node address, router, authentication, or product policy.

An application should normally depend on a WLC-generated runtime and
`wirelink/link.h`. An adapter implements `wirelink/port.h`. Direct use of
`wirelink/rpc.h` is for generators and advanced integrations.

## Linkable Targets

| Target | Purpose | Default firmware cost |
| --- | --- | --- |
| `Wirelink::wirelink` | C11 link core and optional runtime primitives | Core selection |
| `Wirelink::loopback` | In-memory native-packet adapter | None unless linked |
| `Wirelink::diagnostics` | Key/value formatting into caller storage | None unless linked |
| `Wirelink::host` | Optional C++20 threaded host executor | None unless enabled/linked |
| WLC-generated target | Schema codec, bindings, or one role runtime | Only selected schema/profile |

Astrial and Asio adapters are source-integrated platform targets rather than
part of the installed core package.

## Header Selection

| Header | Intended owner |
| --- | --- |
| `link.h` | Endpoint/application owner: init, send, poll, events, TX results |
| `pump.h` | Owner-loop composition and deadline merging |
| `port.h` | Adapter/driver producer and asynchronous TX completion |
| `latest.h`, `fifo.h` | Lock-free SPSC retained-message storage |
| `outbox.h` | Externally serialized, coalescing outbound queue |
| `rpc.h` | Low-level RPC engine; generated runtime is preferred |
| `bulk.h` | Sequential object-transfer sender/receiver state machines |
| `diagnostics.h` | Optional formatting of snapshots and RPC outcomes |
| `frame.h`, `cobs.h`, `crc.h` | Protocol tooling, tests, and specialized ports |
| `codec.h`, `profile.h`, `span.h`, `types.h` | Shared public vocabulary |

`wirelink/wirelink.h` is a compatibility umbrella over `link.h` and `port.h`.
New code should include the narrow header it owns; ordinary application logic
should not gain producer APIs merely for convenience.

## Core Endpoint Lifecycle

The required order is:

1. Fill `wl_config_t`; call `wl_config_requirements()`.
2. Allocate stable caller-owned storage and call `wl_init()`.
3. Bind and activate exactly one adapter/ingress family.
4. From one consumer, send and run `wl_poll()` or `wl_pump_step()`.
5. Release every RX event and take every terminal reliable TX handle once.
6. Stop producers and quiesce the adapter before discarding or reusing storage.

There is no core `deinit()`: the core owns no external resource. Quiescing the
adapter is the resource boundary. An initialized opaque context must not be
copied or moved.

### Configuration and session

Peers agree out of band on envelope, integrity, payload bound, and transmission
unit bound. A nonzero `session_id` is a boot/incarnation identifier for reliable
traffic, not a node address. The application provisions it from randomness or
persistent monotonic state and must not reuse it while old traffic can survive.

### Sending

`wl_send_unreliable()` reports local submission only. `wl_send_reliable()`
returns a handle whose terminal event means link delivery or failure, never
application execution. `wl_tx_status()`, `wl_tx_cancel()`, and `wl_tx_take()`
operate on that retained transaction; only `take()` releases a terminal slot.

Generated typed senders use `wl_tx_payload_claim()`/`commit()` internally to
encode into Wirelink-owned storage. `abort()` closes a failed application claim.
Applications should prefer the generated typed operation unless transporting
already encoded bytes.

### Polling and borrowed events

`wl_poll()` performs bounded progress and returns at most one event. An RX
payload is borrowed until exactly one `wl_event_release()`. A reliable terminal
handle is retained until exactly one `wl_tx_take()`. `wl_poll_get_hint()` is
side-effect free and returns immediate-work plus a relative deadline; it does
not replace the next externally signalled adapter wake.

## Port and Adapter Boundary

An adapter binds one `wl_sink_fn`. `WL_SINK_SENT` consumes bytes synchronously;
`WL_SINK_STARTED` borrows them until one matching `wl_tx_complete()`;
`WL_SINK_BUSY` is retryable backpressure; `WL_SINK_FAILED` is terminal.

Choose exactly one RX ingress family per initialized context:

- copied stream: `wl_feed_bytes()`;
- reserved/direct stream: `wl_rx_reserve()`/`commit()`;
- DMA stream: claim, one or more publish calls, then finish/abort;
- copied packet: `wl_feed_unit()`;
- queued/direct packet: initialize the unit queue, then claim/commit/abort.

Callbacks and ISRs publish bytes/completions and wake the owner. They do not
decode, call handlers, send recursively, or run `wl_poll()`. The common
initialize/activate/service/quiesce mapping is in
[`adapters.md`](adapters.md).

## Owner Pump

`wl_pump_step()` composes adapter service, bounded event dispatch, application
progress, and default event cleanup using separate adapter/application
contexts. An event callback returns `CONSUMED` only after it released the RX
event or took the terminal handle; `UNHANDLED` delegates that action to the
pump. `wl_pump_get_hint()` merges core, adapter, and application relative
deadlines. `wl_pump_quiesce()` delegates adapter shutdown.

The pump is a synchronous helper, not a scheduler or task. The application
still owns wake primitives, fairness budgets, clock sampling, and shutdown
ordering.

## Application Runtime Primitives

All primitives use caller-owned storage and explicit ownership transitions:

- `LATEST`: three SPSC slots; unread state may coalesce; reads are borrowed.
- `FIFO`: bounded SPSC order; full rejects new data; reads are borrowed.
- `outbox`: serialized LATEST-per-message lanes with copy-out acquisition.
- RPC client/server: bounded correlation, deadlines, replay, and response
  ownership; no durable exactly-once guarantee.
- Bulk: one sequential transfer with application status acknowledgement; no
  object-sized staging or fragment fields in the v1 link header.

Each component exposes init, lifecycle operations, state/stat snapshots, and
where needed a side-effect-free deadline hint. Reset is externally serialized
and is invalid while a borrow/claim remains active.

## Default Endpoint Assembly

Ordinary applications use a generated `*_endpoint_t`, not a hand-written struct
of buffer pointers. The type contains a generic `wl_endpoint_t`, runtime state,
and statically sized storage. Its private members are not application API.
`*_endpoint_init()` supplies native-packet/CRC32C defaults; `init_config()` allows
explicit transport settings, RPC roles, policies, and callbacks. Objects must
start zero-initialized and must not move until closed.

`endpoint_send_<message>()` adopts the retained profile's delivery mode;
`endpoint_read_<message>()` copies LATEST/FIFO values and releases their leases.
`endpoint_step()` composes adapter service, dispatch, terminal reclamation, and
runtime progress with a bounded event budget. It does not create a thread.
`endpoint_handle()` is the supported adapter entry, and `endpoint_runtime()`
provides advanced borrowed-read/RPC access. Do not separately consume events
or TX handles already owned by this default loop.

The bound covers profile-selected messages; unrelated unbounded messages do not
force allocation. Unbounded or oversized selected messages set
`HAS_DEFAULT_ENDPOINT=0`. Larger queues, custom arenas, or DMA placement use the
existing manual storage path. See [design and limits](default-endpoint.md).

Managed RPC adds generated `*_call_t`, `*_result_t`, and `*_request_token_t`:
business messages contain no operation/status metadata. `endpoint_*_call()` starts
a call, `inspect()` returns its typed result, `release/cancel()` manage it, and
`complete/reject()` reply. Private-in-use handles validate endpoint ownership and
lifetime. Explicit field mappings remain a separate interoperability mode; the
two payload formats cannot be mixed. See the [RPC contract](rpc-runtime.md).

## WLC-Generated Surface (ABI 20)

WLC deliberately splits three concerns:

1. `<module>.h/.c`: message model, clear/encode/decode, size bounds;
2. `<module>_bindings.h/.c`: typed direct router and typed send operations;
3. `<runtime>_runtime.h/.c`: only the retained/RPC policy selected by one
   binding profile.

Use one codec target and generate separate named host/device runtimes against
it. Generated artifacts must match compiler version, codegen ABI, schema
identity, and binding-profile identity. ABI 20 adds managed RPC to the default endpoint
facade above; advanced runtime families remain:

| Family | Generated pattern |
| --- | --- |
| Configuration | `*_runtime_config_defaults()`, `*_runtime_config_enable_{client,server}()` |
| Storage | `*_runtime_requirements()`, optional `*_default_storage_descriptor()` |
| Initialization | `*_runtime_init()` or bring-up-only `*_runtime_init_checked()` |
| Dispatch | `*_runtime_dispatch_event()` plus tagged result helpers |
| Retained state | typed `*_latest_acquire/release`, `*_fifo_acquire/release` |
| RPC client | typed `*_client_start/inspect/decode/release` |
| RPC server | typed handler and `*_server_complete/reject` |
| Progress | `*_runtime_poll/service/get_deadline_hint` |
| Composition | `*_runtime_pump_init()` and `*_runtime_pump_hooks()` |
| Peer transition | `*_runtime_peer_observe()` and `*_runtime_peer_observation_take()` |

`runtime_init_checked()` identifies a rejected field and required/provided
capacity. Normal firmware may use `runtime_init()` so diagnostic strings and
validation code can be garbage-collected. Reliable server request dispatch
automatically observes its peer session, drops old-session RPC state, requests
cancellation of detached responses, and sets `detail.rpc.peer_changed`.
Applications take the saved observation to revoke product leases. Explicit
`runtime_peer_observe()` covers reliable non-RPC traffic that establishes the
same product session.

Generated results use `domain`, `detail_kind`, and `event_consumed`. Use
`*_runtime_result_ok()`, `*_result_str()`, and the tag-checked detail accessors;
do not inspect the wrong union member or parse diagnostic strings. Treat the
post-init `*_runtime_t` wiring as runtime-owned state rather than reaching into
its component pointers.

## Diagnostics Boundary

`Wirelink::diagnostics` appends stable `record key=value` text into a
`wl_diag_writer_t`. It performs no allocation or I/O. Truncation leaves a valid
NUL-terminated prefix and records the exact required byte count. Products own
logging, timestamps, severity, snapshot cadence, and transport of the text.

## Compatibility Boundary

Wire protocol, C library version, WLC compiler version, generated-code ABI,
schema identity, and profile identity are separate compatibility domains. The
compact-v1 byte vectors are frozen. Generated ABI is exact-match and artifacts
are regenerated as a unit.

Public fixed-width enum-like domains and opaque storage sizes are intended for
the 1.x ABI. Pointer/`size_t` structures are architecture-specific. New 1.x
capabilities should use new functions and standalone structures rather than
append fields to closed v1 configuration/event structures. See
[`compatibility.md`](compatibility.md).

## Pre-1.0 Review Points

Managed RPC may need to echo a client session identity to guarantee response
freshness across client reconstruction. Local handle generations do not solve
wire-ID reuse with stale replies; see the [RPC contract](rpc-runtime.md). Such a
change would affect RPC payloads, independently of the compact-v1 link frame.

The following are intentionally visible for the API review rather than hidden
behind documentation:

- whether the compatibility umbrella should continue exposing `port.h`;
- whether every enum-like public domain, including pump disposition, should
  use an explicit fixed-width typedef consistently;
- whether `int`, `wl_err_t`, `wl_rpc_err_t`, `wl_bulk_err_t`, and generated
  result domains are separated at the right boundaries;
- whether generated `*_runtime_t` exposes more wiring state than applications
  should see after initialization;
- whether one broad `diagnostics.h` is preferable to narrower formatter
  headers for firmware include hygiene; and
- whether manual peer observation for non-RPC reliable traffic is sufficiently
  discoverable without introducing a general session object.

These questions can change source API before 1.0 without changing compact-v1
wire bytes.
