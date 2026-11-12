# Current development checks (0.13)

The allocation-free C11 kernel is in `src/`, public headers in
`include/wirelink/`, platform I/O in `adapters/`, and optional host/thread/wait
code in `runtime/host/`. WLC is an independent repository. Keep operating
systems, heap allocation, threads and clock reads outside the core.

Use strict C11, two-space C indentation, SPDX headers and warning-clean
GCC/Clang builds. Public symbols use `wl_`; enum/macro names use `WL_`.
Keep generated C deterministic, and preserve public codec domains and frozen
conformance vectors. Do not update unrelated dirty files or external vendor
copies when updating a consumer.

## Development checks

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
cargo test --manifest-path /path/to/matching-wlc/Cargo.toml
cargo clippy --manifest-path /path/to/matching-wlc/Cargo.toml --all-targets --all-features -- -D warnings
cargo fmt --manifest-path /path/to/matching-wlc/Cargo.toml --check
```

Enable `WIRELINK_BUILD_WLC_EXAMPLES` for the generated C tutorials.
Build the documentation site through its npm scripts. Application profiling
and its tests are separate from core execution.

Use a matching local WLC 0.13 executable and fresh build/output directories.
Relevant optional configurations:

| Check | CMake options |
| --- | --- |
| C tutorials | `WIRELINK_BUILD_WLC_EXAMPLES=ON` |
| Platform instance and USB application fixtures | tutorials plus `WIRELINK_BUILD_PLATFORM=ON` |
| Owner command/stop model | `WIRELINK_BUILD_OWNER_PASS_TESTS=ON` |
| Host Executor | `WIRELINK_BUILD_HOST_RUNTIME=ON` |
| C++ UDP Session | `WIRELINK_BUILD_CPP_BINDINGS=ON`, with matching Asio include path |
| C-owned C++/Python clock/fixed-pool bridges | `WIRELINK_BUILD_FOREIGN_TESTS=ON` |
| Packet adapters | `WIRELINK_BUILD_ASIO_UDP_ADAPTER`, `WIRELINK_BUILD_ASTRIAL_ADAPTER`, `WIRELINK_BUILD_ASTRIAL_USB_ADAPTER` |
| Current fuzzers | `WIRELINK_BUILD_FUZZERS=ON`, Clang with libFuzzer/ASan/UBSan |
| Packet transport tools | `WIRELINK_BUILD_PACKET_TOOLS=ON`, both Astrial adapters and platform |

Configure sanitizers in a separate tree. Do not substitute C++ atomic storage
for C atomics to make a test compile. C++ concurrency fixtures create/join
threads while the C fixture retains the actual atomic ownership operations.
Test meaningful boundaries: snapshot before return, four calls/replies,
reservation before side effects, one terminal, stale token/result rejection,
BUSY wakes, separate consumption/drain and real stop/join. Preserve failed
attempts and distinguish software driver models from hardware qualification.

From an initialized Zephyr workspace:

```sh
west twister -T /path/to/wirelink/tests/zephyr/unit \
  -T /path/to/wirelink/tests/zephyr/integration \
  -p unit_testing -p native_sim -p qemu_cortex_m3 -p qemu_riscv32
```

Pure core cases are Ztest `type: unit`; adapters/emulators are integration
cases. Additional x86_64 cases and board sample builds have their own platform
allow lists. Static platform filters are reported separately from runtime
skips. A sample build is not a physical UART/USB test. Check the selected Zephyr
UART TX-complete API; the current CDC sample is a rejection probe.

Installed-package checks must consume a fresh prefix, including C-only
CMake/pkg-config, public C++ headers, generated v2 endpoints and enabled
adapters. SDK tests include installed C++ consumers and Python wheels against
independent C peers. Record which Python ABI, OS and wheel actually ran; a
single local wheel is not a manylinux or all-platform release qualification.

Benchmark tooling tests run without timing dependencies. See the
[benchmark entry points](../benchmarks/README.md) and the
[kernel contract](static-endpoint-kernel-api.md). Record the source, build
configuration and validation commands with each change.
