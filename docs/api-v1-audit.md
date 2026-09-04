# Wirelink v1 C API audit

Status: superseded as an ABI freeze by `api-boundary.md` during pre-1.0
convergence. It remains a record of the 0.9 review and release checks.

The compact-v1 transmission units remain frozen by `conformance-v1.md`.
Public C declarations may still change before 1.0; do not treat the historical
layout list below as the final 1.x compatibility policy.

## Retained ABI boundary

The installed core surface remains the `Wirelink::wirelink` target and the
headers explicitly listed by the top-level install rule. `wl_ctx_t` stays an
opaque 896-byte, `max_align_t`-aligned object. The implementation may consume
more of that reserved storage during 1.x, but neither its public size nor its
alignment may change.

Public enum-like types use fixed-width `int32_t` storage. Their layout is the
same with and without compiler options such as `-fshort-enums`. Structures
containing pointers or `size_t` remain specific to the target architecture;
Wirelink does not promise that a 32-bit binary and a 64-bit binary share a C
ABI.

The following public structures are closed for v1 and must not gain fields in
the 1.x line:

- `wl_span_t`, `wl_codec_bytes_t`, and `wl_codec_string_t`;
- `wl_frame_header_t`, `wl_wire_packet_t`, and `wl_frame_view_t`;
- `wl_config_t`;
- `wl_storage_t` and `wl_storage_requirements_t`;
- `wl_event_t` and `wl_tx_result_t`;
- `wl_poll_hint_t`;
- `wl_rx_counters_t`, `wl_rx_dma_claim_t`, `wl_rx_unit_claim_t`,
  `wl_rx_unit_queue_config_t`, and `wl_tx_payload_claim_t`.

New capabilities must therefore use additive functions and new standalone
types rather than appending fields to existing structures. Multiple TX slots,
a sliding window, negotiated profiles, and fragmentation require a separately
designed post-v1 storage/API extension.

## Lifecycle decisions

`wl_init()` is the first operation on a context. An adapter must be stopped
and must have returned every borrowed RX/TX buffer before that storage is
reinitialized. Wirelink cannot inspect arbitrary uninitialized C storage; a
zero-initialized context produces `WL_ERR_NOT_INITIALIZED` from context APIs
until `wl_init()` succeeds.

After initialization, one consumer owns sends, polling, event release,
transaction queries, TX completion, and adapter service. Exactly one producer
owns the selected RX feed/reserve/DMA lifecycle. The core does not add a lock
or silently serialize calls outside those roles.

Sink rebinding is a quiescent operation. A platform adapter must cancel or
finish its transfers before unbinding its sink; changing the callback does not
cancel an already borrowed TX unit. `wl_feed_recover_reset()` is a COBS-stream
consumer recovery operation and reports `WL_ERR_NOT_SUPPORTED` for native
packet and length-prefixed profiles.

`wl_poll()` clears its output event before reporting `WL_ERR_NO_DATA`, so a
caller cannot accidentally reuse a previously returned payload pointer. An RX
payload remains valid only until its matching `wl_event_release()`.

Session IDs continue to be provisioned by the application. Sequence
exhaustion is not hidden by silently reusing a session: stop the adapter,
choose a new nonzero session ID, and reinitialize the context.

## Implemented additive APIs before 1.0

Two additions are justified by the performance and scheduler contracts. They
are implemented and validated before the 1.0 tag rather than being added
speculatively afterward.

### Borrowed TX payload construction

A claim/commit/abort family exposes retained payload construction storage so a
WLC encoder writes directly into it. Native profiles use the final TX unit;
stream and length-prefixed profiles use the core's retained payload region.
The claim is
consumer-owned, at most one may exist, ordinary send calls must reject an
active claim, and a failed commit must leave either a retryable claim or a
fully aborted claim—never ambiguous ownership. Reliable retransmission still
uses Wirelink's stable encoded TX unit.

This API removes the generated-code temporary-to-Wirelink copy and, for native
packets, the payload staging copy. COBS output remains a distinct encoded byte
stream.

### Poll scheduling hint

`wl_poll_get_hint()` fills the standalone, fixed-width `wl_poll_hint_t` without
executing protocol work. `work_pending` is zero or one; `next_deadline_ms` is
the wrap-safe relative delay from the supplied `now_ms`, with
`WL_POLL_NO_DEADLINE_MS` (`UINT32_MAX`) denoting no timed protocol deadline.
The fields are independent: queued RX can coexist with a future ACK deadline,
and a due deadline reports both `work_pending = 1` and a zero delay.

Immediate work includes an already queued event, a complete COBS unit, an RX
overflow notification, a committed native unit, and a due ACK retry or retry-
exhaustion transition. A leased RX event gates later RX work until release,
but does not hide a TX event or deadline. Partial COBS input has no immediate
work and relies on the producer's next activity notification.

Queued control or DATA after `WL_SINK_BUSY`, and work blocked behind an
in-flight unit, deliberately do not report immediate work. The next externally
observed writable or I/O-completion activity wakes the consumer, which calls
`wl_poll()` once before querying the hint again. This gives each wake one retry
without turning backpressure into an unbounded zero-delay loop. Platform
adapters remain responsible for RX, TX-completion, and writable notifications;
the core owns no semaphore, file descriptor, or RTOS wait primitive.

## Release gates

The installed-package checks now compile the public surface as strict C11,
strict C++20, and C11 with `-fshort-enums`. Regular GCC and Clang builds also
enforce a 256-byte fixed-frame ceiling on the frame and protocol translation
units, covering `wl_frame_encode()` and `wl_poll()` without applying the guard
to sanitizer-instrumented builds. The ESP32-S3 USB sample's normal and CPU
telemetry configurations are part of the Zephyr build-only CI matrix.

Before changing the version to 1.0:

1. exact compact-v1 conformance vectors must remain unchanged;
2. normal and `-fshort-enums` installed-package consumers must compile;
3. the borrowed TX and scheduling APIs and their package ABI checks must remain
   covered by the release test matrix;
4. public headers must compile as strict C11 and C++20;
5. unit, native simulation, QEMU, sanitizer, fuzz, and supported adapter tests
   must pass; and
6. MCU stack-usage limits must guard the frame encoder and polling hot paths.
