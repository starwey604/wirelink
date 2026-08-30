# Changelog

All notable Wirelink changes are recorded here. The project uses semantic
versioning after 1.0; compatibility expectations for the release-candidate
line are described in [`docs/compatibility.md`](docs/compatibility.md).

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
