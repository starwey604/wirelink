# Changelog

All notable Wirelink changes are recorded here. The project uses semantic
versioning after 1.0; compatibility expectations for the release-candidate
line are described in [`docs/compatibility.md`](docs/compatibility.md).

## Unreleased

### Typed application runtime

- Added separately linkable WLC-generated typed routers, scratch send
  wrappers, and a native-packet claim/encode/commit path; valid RX events are
  released exactly once across success and every routing failure domain.
- Added an allocation-free lock-free SPSC `LATEST` mailbox with three-slot
  ownership transfer, stable borrowed reads, direct decode into write claims,
  coalescing counters, and generation wrap coverage.
- Added an allocation-free lock-free SPSC `FIFO` with caller-sized storage,
  stable borrowed reads, reject-new backpressure, and lifecycle statistics.
- Added a caller-sized RPC client/server runtime that keeps link delivery
  separate from application completion and supports asynchronous completion,
  deadlines, cancellation, duplicate suppression, response replay, and
  explicit cache policy.

### API and protocol hardening

- Made every public enum-like type explicitly `int32_t`, recorded the Linux
  x86-64 v1 structure layout, and added an installed-package
  `-fshort-enums` CI gate plus a strict C++20 public-header consumer.
- Standardized pre-initialization errors, idle-event clearing, COBS-only
  recovery, ACK timeout bounds, and the single-slot `WL_SINK_BUSY` queueing
  contract.
- Added a side-effect-free poll scheduling hint with wrap-safe relative ACK
  deadlines, complete-RX detection, and non-spinning sink-backpressure rules.

### Performance and validation

- Replaced the frame encoder's maximum-sized raw stack buffer with direct and
  span-streamed encoding while preserving exact v1 bytes and overlapping input
  support; the ESP32-S3 stack frame fell from 2,128 to 96 bytes.
- Added 64-bit, wrap-safe USB sample CPU attribution without instrumenting the
  normal image, versioned its CSV contract as v3, and retained ESP32-S3 RTT and
  cycle measurements.
- Expanded deterministic ARQ fault injection, cancellation/time-wrap cases,
  threaded SPSC/backpressure stress, API lifecycle tests, and USB sample build
  coverage.
- Added 256-byte compiler stack-frame gates for frame encoding and protocol
  polling, plus ESP32-S3 normal/telemetry USB sample builds in Zephyr CI.

## 0.9.0 - 2026-08-30

First release candidate for the Wirelink v1 protocol and C API.

### Protocol and API

- Froze v1 DATA and ACK bytes across native packet, 16-bit bus length, and
  COBS stream envelopes with none, CRC16/CCITT-FALSE, and CRC32C integrity.
- Added stop-and-wait reliable TX, duplicate suppression, explicit event
  leases, RX counters, and deterministic frame rejection.
- Made `wl_ctx_t` opaque fixed storage and standardized `message_id` naming.
- Added caller-sized storage requirements and direct two-claim DMA ingress.

### Platforms and performance

- Added the Zephyr asynchronous UART/DMA adapter with deferred ISR completion,
  backpressure recovery, shutdown, cache hooks, and ESP32-S3 line-idle timing.
- Added the Astrial serial adapter for Linux, macOS, and Windows with borrowed
  SPSC reads and deferred asynchronous writes.
- Added custom Zephyr Vendor Bulk and Astrial/libusb transports with direct
  ring ingress, borrowed TX, explicit ZLP handling, and reconnect support.
- Added the generic Zephyr UART IRQ fallback and a USB CDC ACM endpoint for
  boards where a custom class or DMA path is unavailable.
- Selected the internal atomic SPSC BipBuffer after host and ESP32-S3
  comparison with LwRB; retained reproducible latency and throughput results.

### Tooling and validation

- Defined the WLC v1 schema/codec contract and compatibility fixtures for
  generated allocation-free C11 codecs.
- Added exact conformance vectors, Clang COBS/frame/protocol/codec fuzzers, and
  sanitizer CI.
- Added Linux, macOS, Windows, native simulation, Cortex-M, RISC-V, and x86-64
  QEMU coverage.
- Added relocatable CMake and pkg-config installation plus bare-metal, Astrial,
  and Zephyr UART/DMA examples.
- Added comparable Raw Bulk, full Wirelink Bulk, and CDC IRQ RTT benchmark
  firmware and host tooling, with a fixed ESP32-S3 measurement procedure.
