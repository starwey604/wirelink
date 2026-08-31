# Wirelink v1 C API audit

Status: completed against the `0.9.0` release candidate on 2026-08-31.

This audit fixes the C source and binary surface that may be carried into the
1.x line. It was refreshed after the final pre-release compact-v1 and
zero-copy RX/TX iteration; transmission units are now frozen by
`conformance-v1.md`.

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

## Approved additive APIs before 1.0

Two additions are justified by the performance and scheduler contracts, but
they must be implemented and validated before the 1.0 tag rather than added
speculatively afterward.

### Borrowed TX payload construction

A claim/commit/abort family exposes the payload region in the final native TX
unit so a WLC encoder writes directly into it. The claim is
consumer-owned, at most one may exist, ordinary send calls must reject an
active claim, and a failed commit must leave either a retryable claim or a
fully aborted claim—never ambiguous ownership. Reliable retransmission still
uses Wirelink's stable encoded TX unit.

This API removes both the generated-code temporary-to-Wirelink copy and the
native payload staging copy. It does not
promise that COBS framing itself can borrow application bytes; COBS output is
necessarily a distinct encoded byte stream.

### Poll scheduling hint

An additive query may report whether consumer work is immediately pending and
the wrap-safe delay until the next protocol deadline. `UINT32_MAX` can denote
the absence of a timed deadline. Platform adapters remain responsible for
waking the consumer on RX and TX completion; the core does not own a semaphore
or an RTOS wait primitive.

The query must account for reliable ACK timeout/retry and queued control/data
work. It cannot turn `WL_SINK_BUSY` into an unbounded busy loop: an adapter
that can become writable asynchronously must publish an activity notification.

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
3. the borrowed TX and scheduling APIs must either ship with complete tests or
   be explicitly deferred to a later additive 1.x release;
4. public headers must compile as strict C11 and C++20;
5. unit, native simulation, QEMU, sanitizer, fuzz, and supported adapter tests
   must pass; and
6. MCU stack-usage limits must guard the frame encoder and polling hot paths.
