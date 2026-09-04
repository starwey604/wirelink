# Onboarding API and Architecture Findings

This backlog records friction found while writing and compiling
[`getting-started.md`](getting-started.md), using libcsp's basics and loopback
documentation as the usability reference. It is an optimization list, not a
commitment to turn Wirelink into a routed network stack.

## Outcome

Wirelink can already build a complete allocation-free communication pair from
an empty application: the compiled quickstart sends typed unreliable state and
completes a reliable RPC with session-scoped duplicate protection. The model is
coherent, but the first example needs substantially more lifecycle and storage
code than the protocol concepts suggest. Most of that cost comes from missing
composition helpers, not from missing wire behavior.

## P0: Resolve before freezing the next public API

### Make event ownership composable

Resolved in codegen ABI 15. `wl_pump_event_fn` now returns `UNHANDLED` or
`CONSUMED`, and generated dispatch reports `result.event_consumed` after
releasing RX or reclaiming a matching RPC terminal handle. A pump bridge can
therefore delegate fallback ownership without a second release/take.

### Publish a self-consistent compiler/library pair

Current source declares WLC `0.4.0` with codegen ABI 14, while the immutable
`v0.4.0` release emits ABI 12 and Wirelink's download hashes still select those
assets. Release WLC `0.5.0`, update its release smoke check from ABI 12, then
pin its five host assets and hashes from Wirelink `0.10.0`. Verify an installed
consumer without a locally injected `WLC_EXECUTABLE`.

## P1: Reduce the first-integration burden

### Generate an owner-loop bridge

Implemented: generated `*_runtime_pump_init()`/`*_runtime_pump_hooks()` bridge
dispatch, RPC service, and RPC deadlines into the public pump. Pump hooks keep
separate adapter and application contexts, so applications can add transport
service/quiesce/deadline callbacks without another wrapper object.

### Add role-focused runtime initialization

The flat generated config mixes LATEST/FIFO, client, server, cache, timeout,
scratch, and handler fields. Provide generated client/server default builders
and named storage recipes. Report which field invalidated requirements rather
than returning only `WL_ERR_INVALID_ARG` or `WL_ERR_BUF_TOO_SMALL`.

### Support static sizing without guessed arenas

`runtime_requirements()` and `wl_config_requirements()` are correct but only
available at runtime. Static firmware examples must reserve a conservative
array, check it, and carry an alignment union. Generate conservative capacity
and alignment macros, or a configuration-to-storage declaration mechanism,
so insufficient memory becomes a build failure.

### Stop temporarily mutating RPC inputs

Generated client start and server finish restore caller objects, but accept
mutable pointers solely to inject operation ID/status during encoding. This is
surprising and prevents genuinely read-only requests. Generate override-aware
encoders or use owned scratch so public request/response inputs can be `const`.

### Make result handling smaller and discoverable

The generated domain/detail/result union is precise but verbose for normal
paths. Add helpers such as `*_runtime_result_ok()`, `*_runtime_result_str()`,
and checked accessors for RPC/retained details. Keep the full diagnostic result
for advanced users.

### Provide a supported loopback adapter

The first example has to invent a one-unit sink and manually shuttle ACK and
DATA units. A no-allocation in-memory packet adapter would give tests and new
users a standard, hardware-free starting point analogous to libcsp loopback.
It should model backpressure and asynchronous completion, not only the happy
path.

### Allow asymmetric roles without duplicate generated codecs

A binding profile describes receive retention, but a single-process loopback
cannot easily link two profile variants of the same schema because each target
also emits the same codec/binding symbols. Using one shared profile initializes
an unused telemetry mailbox on the sender. Split schema codec generation from
profile runtime generation, or allow retained routes to be disabled per
runtime instance.

### Standardize adapter lifecycle and ingress selection

The core port contract is small, but UART, USB, Astrial, and UDP adapters expose
different start/service/wait/quiesce shapes. Define a documented common
lifecycle table and provide one minimal recipe per envelope. Preserve typed
platform adapters rather than adding virtual dispatch to the C core.

## P2: Improve operational usability

### Document session provisioning and peer transitions

The core correctly refuses session zero, but cannot create a boot-unique ID
without a clock, RNG, or persistence. Supply platform recipes and a small
validation helper. Provide a generated integration path for
`wl_rpc_peer_observe()` so session changes consistently revoke cached RPC and
product leases.

### Add an indexed documentation entry point

README currently links many normative and performance documents but has no
learning path. Link the quickstart first, then concepts, transports, schemas,
RPC, bulk, performance, and protocol reference. Consider generated public API
reference pages, while keeping ownership rules in narrative documentation.

### Offer optional diagnostic helpers

libcsp examples expose interface and connection tables. Wirelink should not add
global registries, but a caller-supplied formatter for link, RX, adapter, RPC,
FIFO, and LATEST counters would make bring-up logs consistent without adding
I/O or allocation to the core.

### State security and topology exclusions early

Wirelink v1 provides integrity checks, not authentication, encryption,
addressing, discovery, routing, broadcast, or access control. Put this near the
quickstart decision table so users do not infer libcsp-like network or security
semantics from the comparison.

## Explicit non-goals

Do not add node addresses, sockets, a routing task, heap-backed packet pools,
or an OS abstraction merely to resemble libcsp. Those features define a
network stack; Wirelink's useful boundary is a deterministic link plus typed,
allocation-free application runtimes. A separate router may be built above
message IDs and adapters if a product actually needs it.
