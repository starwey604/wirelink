# Wirelink 1.0 API boundary

Status: pre-1.0 design contract. The compact-v1 wire format is frozen; the C
source and binary surface remains open until the 1.0 release candidate.

## Layers

Wirelink has one owner-driven link engine and three explicit integration
layers:

1. `wirelink/link.h` is the stable application-facing link API. It owns link
   configuration, payload submission, transaction results, event borrowing,
   and progress/deadline queries.
2. `wirelink/port.h` is the platform-port API. Transport adapters use it to
   bind a sink, publish RX storage, and report asynchronous TX completion.
   Application protocol code should not call producer entry points.
3. `wirelink/{latest,fifo,rpc,bulk}.h` contains optional allocation-free
   application runtimes. WLC-generated typed runtimes are the preferred RPC
   boundary; `rpc.h` is an engine API for generators and advanced integrations.
4. `wirelink/{frame,cobs,crc}.h` contains independent protocol utilities.
   Including `link.h` does not expose their packet representations.

`wirelink/wirelink.h` remains a source-compatibility umbrella over `link.h`
and `port.h`; new code should include the narrow header it owns.

## Compatibility policy

The 1.x line promises compact-v1 wire compatibility and documented source
compatibility. `WIRELINK_PROTOCOL_VERSION`, the C library version, WLC
compiler version, generated-code ABI, schema identity, and binding-profile
identity are separate compatibility domains. Generated artifacts must match
their declared runtime ABI exactly.

Wirelink does not promise that structures containing pointers or `size_t`
have a common ABI across architectures. Fixed opaque storage objects remain
non-copyable after initialization. A future shared-library ABI must introduce
size-tagged configuration entry points rather than freezing every visible
configuration structure indefinitely.

## Ownership boundary

Exactly one consumer owns sends, `wl_poll()`, event release, transaction
queries, TX completion, and adapter service. Exactly one producer owns the
selected RX feed/claim lifecycle. The core owns no heap, thread, lock, clock,
or driver. Ports notify activity; the owner decides when to run using
`wl_poll_get_hint()` and application/runtime deadline hints.

Payload claim/commit/abort is envelope-independent. Native-packet profiles
write into the final unit; stream and length-prefixed profiles write into the
retained payload staging area, avoiding an application scratch copy while
preserving retransmission ownership.
