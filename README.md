# Wirelink

Wirelink is an allocation-free C11 link protocol engine for embedded and
desktop systems. It provides explicit framing, CRC integrity, stop-and-wait
reliability, borrowed RX payloads, and direct SPSC/DMA ingress without owning
hardware, threads, a heap, or a clock.

Platform adapters currently cover Zephyr asynchronous UART DMA and Astrial
serial ports on Linux, macOS, and Windows. WLC-generated C codecs turn typed
schemas into payloads while preserving borrowed `string` and `bytes` fields on
decode.

## Build the core

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core
```

The core target is `wirelink`. Applications provide all persistent storage to
`wl_init()`, drive time through `wl_poll()`, and bind a transport sink through
`wl_set_sink()`.

## Build the desktop serial adapter

```sh
cmake -S . -B build/host \
  -DWIRELINK_BUILD_ASTRIAL_ADAPTER=ON \
  -DWIRELINK_ASTRIAL_SOURCE_DIR=/path/to/astrial \
  -DBUILD_TESTING=ON \
  -DASTRIAL_IO_URING=OFF
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

The adapter target is `wirelink::astrial`. On Linux and macOS the test uses a
pseudo-terminal to verify full-duplex serial traffic, RX backpressure, and a
WLC-generated six-joint command payload.

## Typed payload workflow

The frozen schema and payload rules are in
[`docs/schema-v1.md`](docs/schema-v1.md). A compatible schema pair and its
checked-in generated C artifacts live under
[`tests/fixtures/wlc`](tests/fixtures/wlc). The fixture demonstrates:

- schema compatibility validation before generation;
- allocation-free nested and repeated message encoding;
- Wirelink transmission over the Astrial serial adapter;
- in-place event decode with borrowed `string` and `bytes` fields; and
- old/new decoder behavior plus deterministic malformed-input errors.

## Zephyr tests

From an initialized Zephyr workspace:

```sh
west twister \
  -T /path/to/wirelink/tests/zephyr/unit \
  -T /path/to/wirelink/tests/zephyr/integration \
  -p unit_testing -p native_sim \
  -p qemu_cortex_m3 -p qemu_riscv32 -p qemu_x86_64
```

See [`docs/development.md`](docs/development.md) for concurrency, memory
ownership, DMA lifecycle, adapter, and test contracts. The wire format is
specified in [`docs/protocol.md`](docs/protocol.md).
