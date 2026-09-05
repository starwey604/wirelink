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

Current source declares WLC `0.4.0` with codegen ABI 18, while the immutable
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

Implemented for the common bounded case. Generated config defaults select one
retained/RPC slot, bounded encoded capacities, generation/operation ID one,
disabled roles, zero timeouts, and reject-new cache policy. Explicit client and
server enable helpers leave business expiry policy and handlers to the caller.
ABI 18 adds the optional `*_runtime_init_checked()` path with a rejected field,
required/provided values, and issue text. The ordinary initializer remains the
smaller production path when those diagnostics are not linked.

### Support static sizing without guessed arenas

Implemented for profiles whose RPC request/response payloads all have static
encoded maxima. WLC emits `*_RUNTIME_HAS_DEFAULT_STORAGE`, a conservative
capacity macro, an aligned `*_runtime_default_storage_t`, and a descriptor
helper. Unbounded profiles advertise `HAS=0` and retain the exact runtime
requirements/custom-arena path rather than pretending to have a safe bound.

### Stop temporarily mutating RPC inputs

Implemented in codegen ABI 16. Generated client start and server
complete/reject accept `const` inputs and shallow-copy them into one shared,
typed runtime scratch union before injecting operation ID/status. Encoding is
synchronous on the owner thread; no scratch pointer escapes and no per-service
outbound buffer or heap allocation is added.

### Make result handling smaller and discoverable

Implemented in codegen ABI 17. Every profile emits
`*_runtime_result_ok()` and diagnostic `*_runtime_result_str()` helpers, plus
checked retained/RPC detail accessors only for the variants present in that
profile. Accessors return null on a mismatched tag; the complete result remains
available for advanced diagnostics. Unreferenced diagnostic strings are kept
outside the inline hot path and can be removed by normal function/data-section
linker garbage collection.

### Provide a supported loopback adapter

Implemented as the separately linkable and installed `Wirelink::loopback`
target. It connects two native-packet contexts with one borrowed asynchronous
unit per direction, bounded fair service, explicit RX backpressure, common
adapter counters, and deterministic quiesce. Both compiled examples use it;
the core and firmware images pay no cost unless the target is linked.

### Allow asymmetric roles without duplicate generated codecs

Implemented. `wirelink_wlc_generate_codec()` owns schema codec/binding symbols
once, while each `wirelink_wlc_generate_runtime()` call emits only one profile
runtime. `RUNTIME_NAME` provides distinct filenames and C namespaces when two
asymmetric roles share a process. The installed package consumer links two
profile runtimes against one codec target and verifies that the sender does not
acquire the receiver's retained routes.

### Standardize adapter lifecycle and ingress selection

Implemented in [`adapters.md`](adapters.md). UART, USB, Astrial, UDP, and
loopback retain typed platform APIs while sharing an initialize, activate,
owner-service, and quiesce contract plus one-ingress-path rule. No virtual
dispatch or OS abstraction was added to the C core.

## P2: Improve operational usability

### Document session provisioning and peer transitions

Implemented for point-to-point RPC in ABI 18. Reliable request dispatch
automatically observes the nonzero peer session, discards old pending/cache
state, and cancels detached responses. `*_runtime_peer_observation_take()`
lets product code revoke leases or other authority. Session creation remains a
platform responsibility because the core owns no RNG, clock, or persistence.

### Add an indexed documentation entry point

Implemented in the repository README: quickstart, adapter lifecycle, schema,
application patterns, performance, and protocol references now form an ordered
learning path. Generated API reference pages remain optional future work.

### Offer optional diagnostic helpers

libcsp examples expose interface and connection tables. Wirelink should not add
global registries, but a caller-supplied formatter for link, RX, adapter, RPC,
FIFO, and LATEST counters would make bring-up logs consistent without adding
I/O or allocation to the core.

### State security and topology exclusions early

Implemented near the README introduction and quickstart decision table.
Wirelink explicitly excludes authentication, encryption, addressing,
discovery, routing, broadcast, and access control.

## Explicit non-goals

Do not add node addresses, sockets, a routing task, heap-backed packet pools,
or an OS abstraction merely to resemble libcsp. Those features define a
network stack; Wirelink's useful boundary is a deterministic link plus typed,
allocation-free application runtimes. A separate router may be built above
message IDs and adapters if a product actually needs it.
