# Wirelink

Wirelink is an allocation-free C11 endpoint kernel with static typed routing,
four concurrent RPC calls in each direction, ordinary messages and explicit
packet ownership. Applications supply storage, a single owner, monotonic time
snapshots and a transport. The core owns no OS, thread, heap, clock or driver.

This development tree is **0.13.0 / WLC codegen 0.13 / SDK binding API 3**.
It removes ARQ/ACK execution, automatic retries, replay/TTL caches, deferred
reply tokens, mapped RPC and generic policy/service scheduling. A separately
admitted duplicate request may execute again; applications define retries,
idempotency and long-running jobs. This is a breaking API/ABI/generated-code
change. [Migration](docs/migration-0.13.md) ([中文](docs/migration-0.13-cn.md))
explains the full contract, including borrowed results and shutdown.

Native USB ordinary Compact-v1 DATA bytes and managed v4 RPC metadata remain
compatible. Public tagged SchemaV1 and ordinary PositionalV1 codecs retain
complete schema domains. Removed reliability semantics are not wire guarantees.
Wirelink provides no addressing, routing, discovery, authentication, encryption
or access control; the application selects its transport/security boundary.

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
standalone codec. Old profile syntax and stale output directories fail with
migration errors; separate runtime generation is removed.

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

## Validation and research

Primary C suites are in `tests/zephyr/unit` and `tests/zephyr/integration`, host
ownership/adapter tests in `tests/core`, package consumers in `tests/package`,
and compiler tests in the independent WLC repository. See
[current development commands](docs/development-current.md).

[Benchmarks](benchmarks/README.md) distinguish models, host microbenchmarks and
physical captures. Historical reports and frozen vectors are retained with their
original scope. K4 research results do not automatically describe the final
migrated product; final performance requires a separately identified build and
capture. This repository does not automatically publish, deploy or edit vendor
copies in other projects.

Licensed under [Apache-2.0](LICENSE).
