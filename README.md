# Wirelink

Wirelink is an allocation-free C11 link protocol engine for embedded and
desktop systems. It provides explicit framing, CRC integrity, stop-and-wait
reliability, borrowed RX payloads, and direct SPSC/DMA ingress without owning
hardware, threads, a heap, or a clock.

Wirelink is a point-to-point link and typed application runtime, not a routed
network or security layer. Version 1 does not provide node addressing,
discovery, routing, broadcast, authentication, encryption, or access control.
Use an authenticated transport when the physical link is not trusted.

New users should start with [`docs/getting-started.md`](docs/getting-started.md),
which displays the latest temperature with a complete hardware-free program.
The [Chinese tutorial](docs/getting-started-cn.md) follows the same sequence.
Install WLC independently using [environment setup](docs/installation.md)
([中文](docs/installation-cn.md)); no nested WLC checkout is required.

This release is `0.8.0`, a pre-1.0 library using wire protocol v1.
Exact wire bytes are frozen by the
[`v1 conformance vectors`](docs/conformance-v1.md); compatibility guarantees
and pre-1.0 limits are documented in
[`docs/compatibility.md`](docs/compatibility.md). The C surface contract is in
[`docs/api-boundary.md`](docs/api-boundary.md), and remaining integration work
is tracked in [`docs/onboarding-api-gaps.md`](docs/onboarding-api-gaps.md).

The tutorials require matching WLC 0.8.0 / codegen ABI 32: static schema
composition, borrowed direct routes, service lifecycle hooks, and owned RPC business
values, immediate handlers, synchronous/platform waiting, automatic async call
recycling, optional one-allocation endpoint creation, and automatic session identities.
Managed RPC v2 binds replies to the originating client session; both peers must upgrade.
Use the matching WLC described in [installation](docs/installation.md); CMake
downloads its verified host package automatically. Implementation/H7 handoff evidence is recorded in
[the milestone log](docs/rpc-usability-progress-cn.md).
See the [RPC/telemetry/bulk composition example](examples/04_composed_services/README.md)
and [product integration notes](docs/composed-services-cn.md).

## Documentation path

1. Display the latest temperature in [`getting-started.md`](docs/getting-started.md),
   request a calculation in [`tutorial-rpc.md`](docs/tutorial-rpc.md), save strings
   in [`tutorial-rpc-values.md`](docs/tutorial-rpc-values.md), then build
   your own project with [`tutorial-integration.md`](docs/tutorial-integration.md).
   Nonblocking callers and slow services have separate
   [async](docs/tutorial-rpc-async.md) and [deferred](docs/tutorial-rpc-deferred.md) tutorials.
   Then review [12 modular device services](examples/03_device_service/README.md)
   ([中文](examples/03_device_service/README-cn.md)): shared service definitions,
   send-only telemetry and an executable four-file new-RPC acceptance test.
2. Choose and lifecycle a transport with [`adapters.md`](docs/adapters.md),
   configure an endpoint clock once with [`endpoint-clock.md`](docs/endpoint-clock.md),
   select a default or bare-metal environment with [`session.md`](docs/session.md),
   then integrate its wakeups with [`rpc-platform.md`](docs/rpc-platform.md).
   Optional creation and fixed pools are covered in
   [`endpoint-storage.md`](docs/endpoint-storage.md).
3. Define typed payloads and roles using
   [`schema-v1.md`](docs/schema-v1.md) and the [WLC guide](https://github.com/starwey604/wlc/blob/v0.8.0/README.md).
4. Add retained state, RPC, or objects with
   [`application-layer.md`](docs/application-layer.md),
   [`rpc-runtime.md`](docs/rpc-runtime.md), and
   [`bulk-performance.md`](docs/bulk-performance.md).
5. Add allocation-free bring-up logs with
   [`diagnostics.md`](docs/diagnostics.md), then use [`protocol.md`](docs/protocol.md),
   [`compatibility.md`](docs/compatibility.md), and
   [`conformance-v1.md`](docs/conformance-v1.md) as references.

For a pre-1.0 API review, start with
[`api-boundary.md`](docs/api-boundary.md), validate that model against the
compiled quickstart, then inspect the focused adapter, schema, RPC, retained,
Bulk, and compatibility documents linked from it. The historical
[`api-v1-audit.md`](docs/api-v1-audit.md) is evidence from the earlier 0.9
review, not the current surface definition.
The corresponding Chinese review route starts at
[`api-boundary-cn.md`](docs/api-boundary-cn.md); English documents and public
headers remain normative.

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

For hardware-free performance regressions, see
[`benchmarks/api`](benchmarks/api/README.md) ([中文](benchmarks/api/README-cn.md)):
Google Benchmark CPU/loopback measurements, static endpoint sizes and separate
two-process UDP latency/CPU reports. Dependencies and timing gates are opt-in.
The [mixed-traffic regression](benchmarks/mixed_traffic/README.md) uses deterministic
protocol time to measure telemetry age, update gaps and RPC completion under ACK
loss/backpressure; it is not a CPU benchmark or a hardware latency prediction.
The optional [executor contention matrix](benchmarks/api/EXECUTOR-cn.md) measures
real multi-producer RPC/LATEST handoff; a separate
[H7 CPU harness](benchmarks/zephyr/willow_cpu/README-cn.md) measures the product HIL.
See the [results and synchronization decision](docs/executor-h7-performance-cn.md).
The [owner-pass harness](benchmarks/owner_pass/README.md) counts bounded dispatch,
empty checks and handoffs without timing probes; its
[results](docs/owner-pass-performance-cn.md) distinguish logical work savings from CPU noise.
The [RPC validation benchmark](benchmarks/rpc_validation/README.md) isolates
decode, canonical fingerprint and owned conversion costs on hosts and H7;
[results](docs/rpc-validation-performance-cn.md) include ABI 29 tradeoffs.

The standalone [codec planning benchmark](benchmarks/codec_plan/README.md) covers
field-count/ID-density lookup, fixed arrays and H7 instruction-cache sensitivity
with verified LTO settings. See the [measurement record](docs/codec-plan-performance-cn.md)
and [cursor/key-precomputation follow-up](docs/codec-convergence-performance-cn.md).
The [A-stage fallback decision](docs/codec-fallback-performance-cn.md),
[B/C generator refactor](docs/wlc-refactor-progress-cn.md), and
[post-refactor measurements](docs/wlc-refactor-performance-cn.md) distinguish
compiler CPU gains from unchanged generated runtime code. The paired local
Wirelink/WLC commits are indexed in the [closeout ledger](docs/optimization-commits-cn.md).

The independent [framing benchmark](benchmarks/framing/README.md) compares
single-pass COBS and encoded retry reuse without WLC, Asio or an executor.
It shares a C workload with a [standalone H7 app](benchmarks/zephyr/framing/README-cn.md);
[results and tradeoffs](docs/framing-performance-cn.md) distinguish core CPU work
from end-to-end latency and product CPU utilization.
The [fallback follow-up](docs/framing-fallback-performance-cn.md) records H7
overlap/tight-buffer fixes and the pending idle-host performance check.

The core targets are `wirelink` and its namespaced alias
`Wirelink::wirelink`. Applications provide all persistent storage to
`wl_init()`, drive time through `wl_poll()`, and bind a transport sink through
`wl_set_sink()`.

The installed `Wirelink::loopback` target connects two native-packet contexts
with bounded asynchronous completion and backpressure; see
[`docs/loopback.md`](docs/loopback.md).
The installed `Wirelink::diagnostics` target formats caller-supplied counter
snapshots without heap allocation or I/O; see
[`docs/diagnostics.md`](docs/diagnostics.md). Neither optional target adds code
to firmware that links only `Wirelink::wirelink`.

To install and consume the core as a CMake package:

```sh
cmake --install build/core --prefix /path/to/prefix
```

```cmake
find_package(Wirelink 0.8 CONFIG REQUIRED)
target_link_libraries(my_firmware PRIVATE Wirelink::wirelink)
```

A relocatable `wirelink.pc` file is installed for pkg-config consumers. The
optional Astrial adapter remains source-integrated because Astrial does not yet
publish an installed CMake package target.

The installed package exposes separate codec and runtime generation targets.
WLC is always a host executable, including during a cross-build. This development
tree requires an explicit matching WLC 0.8.0 / ABI 32 executable; see
[environment setup](docs/installation.md). Rust is needed only when building WLC:

```cmake
find_package(Wirelink 0.8 CONFIG REQUIRED)

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
the pinned compatible version, Wirelink fetches the v0.8.0 host archive into
`WIRELINK_WLC_CACHE_DIR` and verifies its fixed SHA256. Windows x86-64,
Linux x86-64/aarch64 and macOS x86-64/arm64 need no Rust installation.
Other hosts build the paired, SHA256-verified source with Rust/Cargo (`--locked`).
Set `WIRELINK_WLC_AUTO_DOWNLOAD=OFF` for offline or hermetic builds and provide
the executable explicitly. Cargo receives the host triple reported by `rustc`,
never the firmware target or an inherited `CARGO_BUILD_TARGET`.

Generated sources are written below the build directory and regenerate when
the schema, profile, compatibility predecessor, or WLC executable changes.
The generated manifest must match Wirelink's pinned compiler version and
codegen ABI before any generated translation unit is compiled.

## C++ and Python bindings

See the [v0.8.0 release guide](docs/release-v0.8.0-cn.md) for supported APIs,
platform wheels, source builds and migration requirements.

WLC now generates complete C++20 and typed Python SDKs for bounded managed
synchronous and asynchronous RPC over UDP. The [calculator guide](examples/06_bindings/GUIDE.md)
shows setup and calls; the [device SDK](examples/07_device_sdk/README.md) exercises
strings/bytes, defaults, optional fields, enums, arrays and nested messages.
`Wirelink::cpp` and `Wirelink::cpp_host` provide common support when
`WIRELINK_BUILD_CPP_BINDINGS=ON`. See the [generator iteration record](docs/bindings-iteration-2-cn.md).
Generated C++ operations support cancellation and owned results; Python `AsyncClient`
supports `asyncio` with bounded completion delivery. See the
[async iteration record](docs/bindings-iteration-3-cn.md). Serial/USB bindings remain later work. The C core and wire formats
are unchanged; generated C still uses ABI 32.

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
supports native packets and explicit legacy COBS framing with configurable
integrity. Endpoint-aware open attaches progress/close; readiness waits avoid
mandatory millisecond polling. See [UDP lifecycle](docs/udp-adapter.md) and the
numbered [telemetry](examples/00_telemetry/) / [calculator](examples/01_rpc/)
process pairs.

The experimental Zephyr native IPv4 UDP adapter is enabled with
`CONFIG_WIRELINK_ZEPHYR_UDP`. It attaches static RX storage, bounded service,
socket/eventfd waiting and backpressure deadlines to generated endpoints.
Functionality is tested; current Zephyr packet-pool exhaustion can still block
a nonblocking send for about one second. See the [UDP contract](docs/zephyr-udp.md)
and [validation/platform gate (Chinese)](docs/zephyr-udp-cn.md) before control-task use.
The [two-device example](samples/zephyr/udp_peer/README.md) provides separate
Zephyr and desktop client/server programs sharing one schema. Optional
`CONFIG_WIRELINK_ZEPHYR_UDP_TIMING` records elapsed-cycle diagnostics, not exclusive
CPU time. Hardware Ethernet acceptance remains pending.

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

For the internal ABI 26 product build status, performance evidence, and outstanding
integration gates, see the [integration closeout (Chinese)](docs/dev-closeout-cn.md)
and the preceding [dev-to-main assessment](docs/dev-main-merge-assessment-cn.md).

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
