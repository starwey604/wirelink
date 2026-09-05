# Wirelink

Wirelink is an allocation-free C11 link protocol engine for embedded and
desktop systems. It provides explicit framing, CRC integrity, stop-and-wait
reliability, borrowed RX payloads, and direct SPSC/DMA ingress without owning
hardware, threads, a heap, or a clock.

New users should start with [`docs/getting-started.md`](docs/getting-started.md),
which builds a hardware-free endpoint pair and demonstrates unreliable typed
telemetry plus a reliable RPC.

The current release is the `0.9.0` release candidate for the v1 wire protocol
and C API. Exact wire bytes are frozen by the
[`v1 conformance vectors`](docs/conformance-v1.md); compatibility guarantees
and pre-1.0 limits are documented in
[`docs/compatibility.md`](docs/compatibility.md). The completed C surface audit
and remaining pre-1.0 API decisions are in
[`docs/api-v1-audit.md`](docs/api-v1-audit.md).

The portable allocation-free loopback adapter provides hardware-free packet
bring-up. Platform adapters cover Zephyr asynchronous UART DMA plus Astrial
serial and native USB bulk ports on Linux, macOS, and Windows. WLC-generated C
codecs and bindings turn typed schemas into payloads, route borrowed RX events,
and submit typed messages. Allocation-free `LATEST`, ordered SPSC `FIFO`, RPC,
and sequential bulk runtimes retain application state and correlate completion
above the frozen v1 link header.

## Build the core

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core
```

The core targets are `wirelink` and its namespaced alias
`Wirelink::wirelink`. Applications provide all persistent storage to
`wl_init()`, drive time through `wl_poll()`, and bind a transport sink through
`wl_set_sink()`.

The installed `Wirelink::loopback` target connects two native-packet contexts
with bounded asynchronous completion and backpressure; see
[`docs/loopback.md`](docs/loopback.md).

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

The installed package exposes separate codec and runtime generation targets.
WLC is always a host executable, including during a
cross-build. A normal online build does not require Rust or a separately
installed compiler:

```cmake
find_package(Wirelink 0.9 CONFIG REQUIRED)

wirelink_wlc_generate_codec(
  TARGET fci_arm_codec
  SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/schema/fci_arm.wl")
wirelink_wlc_generate_runtime(
  TARGET fci_arm_host
  CODEC_TARGET fci_arm_codec
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/schema/host.bind.wl")

target_link_libraries(my_application PRIVATE fci_arm_host)
```

Generate additional role runtimes against `fci_arm_codec`; set a distinct
`RUNTIME_NAME` when more than one role is linked into the same process.
`wirelink_wlc_generate()` remains as a single-runtime convenience wrapper.

WLC resolution checks the call's `WLC_EXECUTABLE`, the project-wide
`WIRELINK_WLC_EXECUTABLE`, and the host `PATH`, in that order. If none names
the pinned compatible version, Wirelink downloads its WLC GitHub Release into
`WIRELINK_WLC_CACHE_DIR` and verifies the archive's fixed SHA256 before use.
Set `WIRELINK_WLC_AUTO_DOWNLOAD=OFF` for offline or hermetic builds and provide
the executable explicitly. Platform selection uses `CMAKE_HOST_SYSTEM_NAME`
and `CMAKE_HOST_SYSTEM_PROCESSOR`, never the cross-compilation target.

Generated sources are written below the build directory and regenerate when
the schema, profile, compatibility predecessor, or WLC executable changes.
The generated manifest must match Wirelink's pinned compiler version and
codegen ABI before any generated translation unit is compiled.

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

On Windows, enable Wirelink's `astrial-usb` vcpkg manifest feature instead of
installing libusb into a machine-wide classic vcpkg tree:

```powershell
$env:VCPKG_ROOT = "C:\src\vcpkg"
cmake -S . -B build/usb-host `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DVCPKG_MANIFEST_FEATURES=astrial-usb `
  -DWIRELINK_BUILD_ASTRIAL_USB_ADAPTER=ON `
  -DWIRELINK_ASTRIAL_SOURCE_DIR=C:\src\astrial `
  -DASTRIAL_IO_URING=OFF
cmake --build build/usb-host --config Release --parallel
```

The pinned manifest installs libusb below the build directory. Astrial's
config-aware imported target links the matching MSVC library and lets vcpkg
copy the correct runtime DLL beside application and test executables.

The `wirelink::astrial_usb` target uses libusb through Astrial. RX transfers
land directly in Wirelink's SPSC ring, and TX transfers borrow Wirelink's
stable encoded unit until completion. It intentionally queues one variable-
length RX claim: a short USB packet must be able to return the unused tail of
the current BipBuffer claim before another claim exists. Deeper queued USB
reads require a staging/copy path and are evaluated separately by benchmarks.
Applications may either poll `service()` for the lowest latency, or call
`wait_for_activity()` after draining `wl_poll()` and `service()` to sleep until
an RX/TX completion. The host benchmark's `--idle poll|wait|hybrid` option
makes the latency/CPU tradeoff directly measurable.

The default `AllCompletions` wake policy is safe for arbitrary traffic. A
request/response application may select `ReceiveOnly` to coalesce the TX and
RX phases into one scheduler wakeup. It must still use a finite wait deadline:
a one-way transmission or missing peer response has no RX completion to wake
the consumer, so timeout processing remains responsible for servicing TX.

## Examples

- [`examples/bare_metal_loopback.c`](examples/bare_metal_loopback.c) is an
  allocation-free native-packet reliable exchange using the supported
  loopback adapter between two static C contexts.
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
- native IEEE `float32`/`float64` values and inline packed numeric arrays;
- a 30-element binary32 control vector encoded in 122 payload bytes;
- separately linkable typed dispatch and send bindings;
- additive `Begin`/`Chunk`/`End`/`Abort`/`Status` messages for sequential bulk
  objects;
- Wirelink transmission over the Astrial serial adapter;
- in-place event decode with borrowed `string` and `bytes` fields; and
- old/new decoder behavior plus deterministic malformed-input errors.

The implemented generated-dispatch, latest-value, ordered FIFO,
application-RPC, and sequential bulk interfaces are described by
[`docs/latest-mailbox.md`](docs/latest-mailbox.md),
[`docs/fifo.md`](docs/fifo.md), and
[`docs/rpc-runtime.md`](docs/rpc-runtime.md). The bulk sender and receiver API
is [`wirelink/bulk.h`](include/wirelink/bulk.h). Their common boundary with
thread ownership, borrowed fields, explicit stream multiplexing, and object
transfer is defined in
[`docs/application-layer.md`](docs/application-layer.md). A Wirelink ACK is a
link-delivery result; application completion always uses an explicit typed
response or status message.

The bulk runtime uses a caller-owned repeatable source and synchronous sink,
without heap allocation or an object-sized protocol buffer. Chunk bytes borrow
the decoded RX event and are valid only during the sink callback. Fresh
nonzero transfer IDs and explicit Abort/reset rules prevent delayed traffic
from reviving an old transfer. The schema fixture is in
[`tests/fixtures/wlc`](tests/fixtures/wlc), the state-machine tests are in
[`tests/zephyr/unit/bulk_sender`](tests/zephyr/unit/bulk_sender) and
[`tests/zephyr/unit/bulk_receiver`](tests/zephyr/unit/bulk_receiver), and the
1 MiB route is covered by
[`tests/zephyr/integration/bulk_transfer`](tests/zephyr/integration/bulk_transfer).

## Zephyr tests

The cross-platform UDP adapter is enabled with
`WIRELINK_BUILD_ASIO_UDP_ADAPTER=ON` and a standalone Asio include directory in
`WIRELINK_ASIO_INCLUDE_DIR`. It hides platform sockets behind a C++20 API and
uses the v1 `COBS_STREAM + NONE` product profile.

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
Cross-transport counters and hardware acceptance rules are defined in
[`docs/adapter-hil.md`](docs/adapter-hil.md).

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

The optional allocation-free runtime microbenchmarks are documented in
[`docs/fifo-performance.md`](docs/fifo-performance.md) and
[`docs/bulk-performance.md`](docs/bulk-performance.md). The bulk benchmark
compares the complete sequential action/Status state machine against a matched
raw sink baseline and keeps its CPU/latency instrumentation reproducible.
