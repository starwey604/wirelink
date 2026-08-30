# Wirelink

Wirelink is an allocation-free C11 link protocol engine for embedded and
desktop systems. It provides explicit framing, CRC integrity, stop-and-wait
reliability, borrowed RX payloads, and direct SPSC/DMA ingress without owning
hardware, threads, a heap, or a clock.

The current release is the `0.9.0` release candidate for the v1 wire protocol
and C API. Exact wire bytes are frozen by the
[`v1 conformance vectors`](docs/conformance-v1.md); compatibility guarantees
and pre-1.0 limits are documented in
[`docs/compatibility.md`](docs/compatibility.md).

Platform adapters currently cover Zephyr asynchronous UART DMA plus Astrial
serial and native USB bulk ports on Linux, macOS, and Windows. WLC-generated C
codecs turn typed schemas into payloads while preserving borrowed `string` and
`bytes` fields on decode.

## Build the core

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core
```

The core targets are `wirelink` and its namespaced alias
`Wirelink::wirelink`. Applications provide all persistent storage to
`wl_init()`, drive time through `wl_poll()`, and bind a transport sink through
`wl_set_sink()`.

To install and consume the core as a CMake package:

```sh
cmake --install build/core --prefix /path/to/prefix
```

```cmake
find_package(Wirelink 0.9 CONFIG REQUIRED)
target_link_libraries(my_firmware PRIVATE Wirelink::wirelink)
```

A relocatable `wirelink.pc` file is installed for pkg-config consumers. The
optional Astrial adapter remains source-integrated because Astrial does not yet
publish an installed CMake package target.

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

## Build the desktop USB bulk adapter

```sh
cmake -S . -B build/usb-host \
  -DWIRELINK_BUILD_ASTRIAL_USB_ADAPTER=ON \
  -DWIRELINK_ASTRIAL_SOURCE_DIR=/path/to/astrial \
  -DASTRIAL_IO_URING=OFF
cmake --build build/usb-host
```

The `wirelink::astrial_usb` target uses libusb through Astrial. RX transfers
land directly in Wirelink's SPSC ring, and TX transfers borrow Wirelink's
stable encoded unit until completion. It intentionally queues one variable-
length RX claim: a short USB packet must be able to return the unused tail of
the current BipBuffer claim before another claim exists. Deeper queued USB
reads require a staging/copy path and are evaluated separately by benchmarks.

## Examples

- [`examples/bare_metal_loopback.c`](examples/bare_metal_loopback.c) is an
  allocation-free native-packet reliable exchange between two static C
  contexts.
- [`examples/astrial_typed_serial.cpp`](examples/astrial_typed_serial.cpp)
  encodes a generated six-joint `ArmCommand` and submits it through Astrial.
- [`samples/zephyr/uart_dma`](samples/zephyr/uart_dma) is a full-duplex
  asynchronous UART/DMA endpoint. Its ESP32-S3 overlay uses UART1 at 3 Mbaud,
  GPIO17 TX and GPIO18 RX.
- [`samples/zephyr/usb_bulk`](samples/zephyr/usb_bulk) is a full-duplex custom
  Vendor Bulk endpoint with direct SPSC-ring RX and a protocol-level echo for
  host integration and latency tests.
- [`samples/zephyr/usb_cdc_irq`](samples/zephyr/usb_cdc_irq) exercises the
  portable interrupt-driven UART fallback over Zephyr CDC ACM with USB DMA
  disabled.

Top-level examples are enabled by default and may be disabled with
`-DWIRELINK_BUILD_EXAMPLES=OFF`.

Build the Zephyr sample from an initialized workspace with:

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/samples/zephyr/uart_dma -- \
  -DDTC_OVERLAY_FILE=boards/esp32s3_devkitc_esp32s3_procpu.overlay
```

For the custom USB endpoint, select `samples/zephyr/usb_bulk` instead. The
sample deliberately sets `CONFIG_UDC_DWC2_DMA=n`, so initial measurements
isolate interrupt-driven Bulk performance before controller DMA is introduced.

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

## Release checks

Host fuzz smoke tests require Clang:

```sh
cmake -S . -B build/fuzz -DCMAKE_C_COMPILER=clang \
  -DWIRELINK_BUILD_FUZZERS=ON -DBUILD_TESTING=ON
cmake --build build/fuzz
ctest --test-dir build/fuzz --output-on-failure
```

The conformance Ztest compares all envelope/integrity combinations against
exact v1 bytes. CI runs it together with the full unit/integration matrix on
native simulation and three QEMU architectures.
