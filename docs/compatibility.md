# Wirelink compatibility policy

Wirelink `0.9.x` is the release-candidate line for protocol v1 and the intended
1.x C API. This policy separates four compatibility domains that evolve at
different rates.

## Wire protocol

Protocol v1 is identified by the base-header version byte `0x01`. Its field
assignments, byte order, envelopes, integrity trailers, ACK behavior, and
decoder rejection rules are normative in [`protocol.md`](protocol.md) and
frozen by [`conformance-v1.md`](conformance-v1.md).

An implementation claiming v1 compatibility must reproduce every exact
transmission unit in the conformance fixture. A change that alters emitted
bytes or makes an existing valid v1 packet invalid requires a new protocol
version and a documented transition strategy. New packet or flag meanings
must use currently reserved assignments; v1 peers continue to reject
unsupported standard meanings. NACK remains reserved.

Link profiles are not negotiated in v1. Peers must agree out of band on the
envelope, integrity mode, MTU, and payload limit.

## C source and binary interface

The installed public surface consists of the headers listed by the install
rule under `include/wirelink/` and the `Wirelink::wirelink` CMake target.
Headers such as `rx_ring_state.h` and all files under `src/` are private even
when visible in a source checkout.

`wl_ctx_t` is opaque, statically allocatable, and aligned through `max_align_t`.
`WL_CONTEXT_STORAGE_SIZE` and that alignment are reserved for the v1 library
line; internal state may change without requiring applications to recompile
their own allocation strategy. Applications must never inspect or persist its
private bytes.

Until 1.0, source or ABI corrections may occur between `0.x` minor releases
and will be recorded in [`CHANGELOG.md`](../CHANGELOG.md). CMake therefore
considers only the same minor `0.9.x` line package-compatible. Starting with
1.0, Wirelink follows semantic versioning: incompatible public C API or ABI
changes require a new major library version. Additive functions and enum
values may appear in minor releases; applications should include a `default`
path when switching over events or errors.

The lifetime and concurrency rules are part of the API contract, not only an
implementation detail. In particular:

- one producer owns RX feed/reserve/DMA calls;
- one consumer owns polling, event release, adapter service, and reset;
- an event payload remains borrowed until `wl_event_release()`; and
- a sink-owned TX pointer remains valid until synchronous completion or the
  matching `wl_tx_complete()`.

## WLC payload schemas

The protocol treats payload bytes as opaque and dispatches them by
`message_id`. WLC schema versions are independent of the Wirelink header
version. Compatible schema evolution follows [`schema-v1.md`](schema-v1.md):
numeric IDs are permanent, removed IDs are reserved, field wire identity is
stable, unknown fields are skipped, and additive optional fields are allowed.

Schema compatibility must be checked before code generation with the previous
schema supplied to WLC. The fixtures under `tests/fixtures/wlc/` are the
cross-version acceptance baseline. Changing a payload incompatibly normally
requires a new message ID; it does not silently redefine the old ID.

## Platform adapters

The C core owns no hardware, operating-system handle, thread, or scheduler.
Adapters may evolve on their platform's cadence while preserving core buffer
ownership and completion rules. The Zephyr adapter API follows the Wirelink C
version. The Astrial adapter is currently a source-integrated C++20 component
and is not part of the installed core ABI.

Real-time performance numbers are platform and configuration specific, not a
compatibility promise. Benchmark changes must retain the exact board,
toolchain, clock, baud rate, payload, ingress mode, and percentile methodology
alongside results.
