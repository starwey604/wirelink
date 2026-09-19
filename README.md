# Wirelink

Wirelink is an allocation-free C11 link protocol engine for embedded and
desktop systems. It provides explicit framing, CRC integrity, stop-and-wait
reliability, borrowed RX payloads, and direct SPSC/DMA ingress without owning
hardware, threads, a heap, or a clock.

Wirelink is a point-to-point link and typed application runtime, not a routed
network or security layer. Version 1 does not provide node addressing,
discovery, routing, broadcast, authentication, encryption, or access control.
Use an authenticated transport when the physical link is not trusted.

This release is `0.8.0`, a pre-1.0 library using wire protocol v1. Exact wire
bytes are frozen by the
[v1 conformance vectors](docs/conformance-v1.md); compatibility guarantees and
pre-1.0 limits are in [`docs/compatibility.md`](docs/compatibility.md); the C
surface contract is in [`docs/api-boundary.md`](docs/api-boundary.md).

## Start here

1. [Getting started](docs/getting-started.md) displays a temperature with a
   complete hardware-free program.
2. The [RPC](docs/tutorial-rpc.md) and
   [bounded values](docs/tutorial-rpc-values.md) tutorials.
3. [Async](docs/tutorial-rpc-async.md) and
   [deferred](docs/tutorial-rpc-deferred.md) callers.
4. [Integrate Wirelink into your project](docs/tutorial-integration.md).
5. The [modular device services example](examples/03_device_service/README.md).

Chinese mirrors: [getting started](docs/getting-started-cn.md),
[installation](docs/installation-cn.md),
[API boundary](docs/api-boundary-cn.md).

## Install

Wirelink needs a matching WLC compiler to generate payload code. See
[environment setup](docs/installation.md) ([中文](docs/installation-cn.md)); no
nested WLC checkout is required. CMake downloads the verified host package
automatically, or you can build WLC from source.

```cmake
find_package(Wirelink 0.8 CONFIG REQUIRED)
target_link_libraries(my_firmware PRIVATE Wirelink::wirelink)
```

A relocatable `wirelink.pc` file is installed for pkg-config consumers.

## Build the core

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core
cmake --install build/core --prefix /path/to/prefix
```

The core targets are `wirelink` and `Wirelink::wirelink`. Applications provide
all persistent storage to `wl_init()`, drive time through `wl_poll()`, and bind
a transport sink through `wl_set_sink()`.
[`docs/development.md`](docs/development.md) documents the concurrency,
ownership, DMA lifecycle, and test contracts.

## Generate payload code

The installed package exposes separate codec and runtime generation targets.
WLC always runs as a host executable, including during a cross-build.

```cmake
find_package(Wirelink 0.8 CONFIG REQUIRED)

wirelink_wlc_generate_codec(
  TARGET myapp_codec
  SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/schema/myapp.wl")
wirelink_wlc_generate_runtime(
  TARGET myapp_host
  CODEC_TARGET myapp_codec
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/schema/host.bind.wl")
target_link_libraries(my_application PRIVATE myapp_host)
```

Generate additional role runtimes against the same codec; set a distinct
`RUNTIME_NAME` when more than one role is linked into the same process.
`wirelink_wlc_generate()` remains a single-runtime convenience wrapper.

WLC resolution checks the call's `WLC_EXECUTABLE`, the project-wide
`WIRELINK_WLC_EXECUTABLE`, and the host `PATH`, in that order. If none names the
pinned compatible version, Wirelink fetches the pinned host archive into
`WIRELINK_WLC_CACHE_DIR` and verifies its SHA256. Set
`WIRELINK_WLC_AUTO_DOWNLOAD=OFF` for offline or hermetic builds. The generated
manifest must match the pinned compiler version and codegen contract before any
generated translation unit is compiled.

Payload rules are in [`docs/schema-v1.md`](docs/schema-v1.md). A compatible
schema pair and its checked-in generated C artifacts live under
[`tests/fixtures/wlc`](tests/fixtures/wlc).

## Transports

An adapter bridges an endpoint to a concrete byte transport.
[`docs/adapters.md`](docs/adapters.md) defines the ownership, readiness, and
lifecycle contract.

- **Loopback** — allocation-free native-packet bring-up, installed as
  `Wirelink::loopback`. See [`docs/loopback.md`](docs/loopback.md).
- **UDP** — cross-platform Asio adapter with native packets and explicit legacy
  COBS framing. See [`docs/udp-adapter.md`](docs/udp-adapter.md) and the
  [telemetry](examples/00_telemetry/) / [calculator](examples/01_rpc/) process
  pairs.
- **Zephyr** — asynchronous UART DMA, interrupt-driven CDC fallback, vendor
  bulk, and native IPv4 UDP. See [`docs/zephyr-udp.md`](docs/zephyr-udp.md).
- **Host serial and USB** — an optional adapter built on the cross-platform
  Astrial serial library.

## Bindings

WLC generates complete C++20 and typed Python SDKs for bounded managed
synchronous and asynchronous RPC over UDP. See the
[calculator guide](examples/06_bindings/GUIDE.md) and the
[device SDK](examples/07_device_sdk/README.md). `Wirelink::cpp` and
`Wirelink::cpp_host` provide common support when
`WIRELINK_BUILD_CPP_BINDINGS=ON`. The
[v0.8.0 release guide](docs/release-v0.8.0-cn.md) (中文) lists supported
platforms and migration requirements.

## Firmware targets

Zephyr is the supported firmware environment. **ESP32-S3 DevKitC**
(`esp32s3_devkitc/esp32s3/procpu`) is the reference board for hardware samples
and firmware benchmarks; `native_sim` and QEMU provide the portable correctness
matrix.

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/samples/zephyr/uart_dma -- \
  -DDTC_OVERLAY_FILE=boards/esp32s3_devkitc_esp32s3_procpu.overlay
```

The samples under [`samples/zephyr`](samples/zephyr) cover UART DMA, vendor USB
bulk, interrupt-driven CDC, a two-device UDP peer, and composed
service/telemetry/bulk endpoints. The experimental native IPv4 UDP adapter is
enabled with `CONFIG_WIRELINK_ZEPHYR_UDP`; read
[`docs/zephyr-udp.md`](docs/zephyr-udp.md) before control-task use.
Cross-transport counters and hardware acceptance rules are in
[`docs/adapter-hil.md`](docs/adapter-hil.md).

From an initialized Zephyr workspace:

```sh
west twister \
  -T /path/to/wirelink/tests/zephyr/unit \
  -T /path/to/wirelink/tests/zephyr/integration \
  -p unit_testing -p native_sim \
  -p qemu_cortex_m3 -p qemu_riscv32 -p qemu_x86_64
```

## Examples

- [Getting started](docs/getting-started.md) — hardware-free temperature
  display.
- [00_telemetry](examples/00_telemetry/) and [01_rpc](examples/01_rpc/) — UDP
  process pairs.
- [02_device_info](examples/02_device_info/) — device information exchange.
- [03_device_service](examples/03_device_service/README.md) — 12 modular device
  services.
- [04_composed_services](examples/04_composed_services/README.md) — static RPC,
  telemetry, and bulk composition.
- [06_bindings](examples/06_bindings/GUIDE.md) and
  [07_device_sdk](examples/07_device_sdk/README.md) — C++ and Python SDKs.
- `examples/bare_metal_loopback.c` — allocation-free native-packet exchange
  between two static contexts.

Top-level examples are enabled by default and may be disabled with
`-DWIRELINK_BUILD_EXAMPLES=OFF`.

## Benchmarks

The opt-in benchmarks under [`benchmarks`](benchmarks/README.md) measure CPU,
memory, and transport cost. They never enter the core, are not correctness
gates, and are not performance guarantees. Machine-specific results are
published on the [documentation site](https://docs.silkenkite.ink/wirelink/).

## Documentation

- **Concepts** — [schema](docs/schema-v1.md), [protocol](docs/protocol.md),
  [application layer](docs/application-layer.md),
  [latest mailbox](docs/latest-mailbox.md), [FIFO](docs/fifo.md),
  [RPC runtime](docs/rpc-runtime.md), [endpoint clock](docs/endpoint-clock.md),
  [session](docs/session.md), [endpoint storage](docs/endpoint-storage.md),
  [RPC platform](docs/rpc-platform.md).
- **Reference** — [API boundary](docs/api-boundary.md),
  [compatibility](docs/compatibility.md),
  [conformance v1](docs/conformance-v1.md), [adapters](docs/adapters.md),
  [diagnostics](docs/diagnostics.md), [adapter HIL](docs/adapter-hil.md).
- **Chinese mirrors** — [API boundary](docs/api-boundary-cn.md),
  [compatibility](docs/compatibility-cn.md),
  [adapters](docs/adapters-cn.md), [Zephyr UDP](docs/zephyr-udp-cn.md).
- **Contributing** — [`docs/development.md`](docs/development.md) and the
  [Chinese documentation style guide](docs/documentation-style-guide-cn.md).

## Release checks

Host fuzz smoke tests require Clang:

```sh
cmake -S . -B build/fuzz -DCMAKE_C_COMPILER=clang \
  -DWIRELINK_BUILD_FUZZERS=ON -DBUILD_TESTING=ON
cmake --build build/fuzz
ctest --test-dir build/fuzz --output-on-failure
```

The conformance Ztest compares every envelope and integrity combination against
exact v1 bytes. CI runs it with the full unit and integration matrix on native
simulation and three QEMU architectures, plus hardware runs on ESP32-S3.
