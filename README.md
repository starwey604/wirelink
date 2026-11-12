# Wirelink

Wirelink is an allocation-free C11 endpoint kernel with static typed routing,
four concurrent RPC calls in each direction, ordinary messages and explicit
packet ownership. Applications supply storage, a single owner, monotonic time
snapshots and a transport. The core owns no OS, thread, heap, clock or driver.

Wirelink 0.13 uses WLC codegen 0.13 and SDK binding API 3. Generated routes,
host runtimes and SDKs share the [kernel and ownership contract](docs/static-endpoint-kernel-api.md).
Applications define retry, idempotency and long-running jobs.

Ordinary messages use Compact-v1 DATA packets. RPC uses managed v4 metadata.
WLC supplies tagged SchemaV1 codecs and explicit ordinary PositionalV1 codecs.
The application selects the transport and security boundary.

## Start here

- [Install the matching local tools](docs/installation.md) ([中文](docs/installation-cn.md)).
- [Telemetry](docs/getting-started.md), [RPC](docs/tutorial-rpc.md),
  [owned results](docs/tutorial-rpc-values.md) and
  [application jobs](docs/tutorial-rpc-deferred.md), with build-backed C listings.
- [Kernel API](docs/static-endpoint-kernel-api.md),
  [adapter lifecycle](docs/adapters.md) and [API map](docs/api-boundary.md).
- [C++/Python generated SDK examples](examples/06_bindings/README.md).

## Build and install

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
cmake --install build/core --prefix /path/to/prefix
```

The default bare-metal loopback example uses checked-in generated C, so WLC is
not required. The installed `Wirelink::wirelink` target links this same kernel
and types; `wirelink.pc` supports C pkg-config consumers.

Build WLC from its matching independent 0.13 source tree when regenerating.
No released 0.13 archive/source pair is assumed, and the helper does not fall
back to an older compiler. Enable `WIRELINK_BUILD_WLC_EXAMPLES=ON` and pass
`WIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc` for the C tutorials.

```cmake
find_package(Wirelink 0.13 EXACT CONFIG REQUIRED)
set(WIRELINK_WLC_EXECUTABLE "/absolute/path/to/matching-wlc")
wirelink_wlc_generate(TARGET app_protocol
  SCHEMA "schema.wl" PROFILE "app.bind.wl")
target_link_libraries(my_application PRIVATE app_protocol)
```

A profile-v2 target contains public codec, one static kernel router and its
selected connector. `wirelink_wlc_generate_codec` without a profile emits only
standalone codec. Generate into a dedicated output directory with matching local WLC.

## Optional integrations

| Component | Source / use |
| --- | --- |
| Platform clock and fresh instance source | `runtime/platform`, `WIRELINK_BUILD_PLATFORM` |
| Bounded host owner thread/wait | `runtime/host`, `WIRELINK_BUILD_HOST_RUNTIME` |
| Owning UDP C++ Session / generated SDK | `bindings/cpp`, `WIRELINK_BUILD_CPP_BINDINGS` |
| Astrial native/NONE USB | `adapters/astrial_usb`, `WIRELINK_BUILD_ASTRIAL_USB_ADAPTER` |
| Astrial COBS/CRC16 serial | `adapters/astrial`, `WIRELINK_BUILD_ASTRIAL_ADAPTER` |
| Asio native/NONE UDP | `adapters/asio_udp`, `WIRELINK_BUILD_ASIO_UDP_ADAPTER` |
| Zephyr UART/USB/UDP/wait | `zephyr/Kconfig`, `samples/zephyr`; same core with `CONFIG_WIRELINK=y` |
| Independent FIFO/LATEST/outbox and fixed-pool allocation | `Wirelink::containers`, optional `WIRELINK_BUILD_STORAGE` |
| Independent frozen frame/CRC/COBS tools | `Wirelink::frame_tools`, `Wirelink::framing_primitives` |

C owns C11 atomic port/FIFO storage; C++ sees opaque handles. RX has two units,
TX separates source-consumed from physically-drained, and driver stop/join plus
kernel quiescence precede destruction. USB boundary faults require real
reset/reconfiguration. Current Zephyr CDC lacks the required TX-complete API and
is explicitly rejected. See [adapter contracts](docs/adapters.md).

## Development and measurement

Primary C suites are in `tests/zephyr/unit` and `tests/zephyr/integration`, host
ownership/adapter tests in `tests/core`, package consumers in `tests/package`,
and compiler tests in the independent WLC repository. See
[development commands](docs/development-current.md).

[Benchmarks](benchmarks/README.md) cover deterministic models, host
microbenchmarks and physical transport measurements.

Licensed under [Apache-2.0](LICENSE).
