# Developing the kernel

[中文](development-cn.md) · [Execution model](kernel.md) · [API reference](kernel-api.md)

For a deadline change, start in `src/kernel.c`, reproduce it with the `kernel`
CTest fixture, then run the packet-port and generated-route checks if their
ownership boundary is affected. For a UART completion change, start in its
adapter and run `static_uart`. Choose checks by the contract you are changing.

[Find the checks for your change](#map-a-change-to-decisive-checks), or start
with the build commands below.

## Build and run the core

Run the following POSIX-shell commands from the Wirelink repository root. Use
CMake 3.21 or later, a C11 compiler with lock-free atomics, a C++17 compiler for
the threaded test fixtures, and Python 3 for the tooling tests. Core GCC/Clang
targets build with warnings as errors. C++ integrations require C++20.

```sh
cmake -S . -B /tmp/wirelink-dev-core -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/wirelink-dev-core --parallel
ctest --test-dir /tmp/wirelink-dev-core --output-on-failure
/tmp/wirelink-dev-core/examples/wirelink_bare_metal_loopback
```

This builds the production kernel, packet port, wire/stream code, independent
tools and checked-in generated loopback. Expect eight bidirectional RPC
completions, two telemetry messages and both endpoints quiescent. No WLC or
hardware is needed. Generated fixtures are added only when a WLC executable is
available; CMake reports when they are omitted. A library-only C build can use
`-DBUILD_TESTING=OFF -DWIRELINK_BUILD_EXAMPLES=OFF`.

For multi-configuration generators, add `--config Release` to build/install
and `-C Release` to CTest. Windows paths use the executable's `.exe` suffix.
MSVC C consumers require C11 atomics; the CMake targets propagate
`/experimental:c11atomics` only to C, not to C++.

## Put a change in the layer that owns it

| Layer and source | Responsibility / example change |
| --- | --- |
| [`src/kernel.c`](../src/kernel.c), [`kernel.h`](../include/wirelink/kernel.h) | Admission, deadlines, event budget, call terminals and snapshot scheduling; change timeout precedence here |
| [`src/packet_port.c`](../src/packet_port.c), [`packet_port.h`](../include/wirelink/packet_port.h) | Atomic RX publication, borrows and token-bound TX leases; change stale-completion validation here |
| [`src/native_none.c`](../src/native_none.c), [`src/rpc_wire.c`](../src/rpc_wire.c) | Canonical DATA and RPC metadata validation; change byte parsing here |
| Independent WLC `src/kernel_codegen.rs`, `src/kernel_profile.rs`, `src/codec_plan.rs` | Typed routes, codec choice and bounded storage; change handler/codec emission in the compiler, then regenerate |
| [`packet_stream.h`](../include/wirelink/packet_stream.h), `src/cobs_crc16_stream.c`, `src/length16_none_stream.c` | Selected stream framing, integrity and partial feed; change delimiter recovery here |
| [`byte_fifo.h`](../include/wirelink/byte_fifo.h), `src/byte_fifo.c`, `src/byte_fifo_cobs.c` | SPSC byte staging and gap propagation; change staging pressure here, not the RPC queue |
| `adapters/`, [`zephyr/Kconfig`](../zephyr/Kconfig) | Driver I/O, physical completion, readiness and stop; change UART drain or USB reset here |
| `runtime/platform`, `runtime/zephyr` | Platform clock, fresh communication identity and wait providers |
| `runtime/host`, `bindings/cpp`, `examples/06_bindings`, `examples/07_device_sdk` | Owner thread, command submission, Session and owning SDK results; change user-facing result delivery here |

The core is allocation-free, strict ISO C11. Keep OS calls, drivers, heap
allocation, threads and clock reads outside it. The owner supplies time and
execution; the adapter publishes I/O. A host wrapper must use the core's call
table/deadlines rather than maintain a parallel RPC state machine.

## Build matching WLC and generated tutorials

WLC is an independent Rust repository. Set `WLC_SOURCE` to its matching source
checkout; it may be located at `wlc/` locally. Rust 1.96.1 is the toolchain used
by the repository's [compiler setup action](../.github/actions/setup-wlc/action.yml).
Cargo needs access to its locked dependencies, either cached or downloadable.
Build output can stay outside both source trees:

```sh
export WLC_SOURCE=/absolute/path/to/matching-wlc
cargo build --release --locked --manifest-path "$WLC_SOURCE/Cargo.toml" \
  --target-dir /tmp/wirelink-dev-wlc
export WLC=/tmp/wirelink-dev-wlc/release/wlc
"$WLC" --version
"$WLC" codegen-contract
```

Expect `wlc 0.13.0` and contract `0.13`, matching
[`WirelinkWlc.cmake`](../cmake/WirelinkWlc.cmake). A version string alone is not
the generated-code contract check. Supply this executable explicitly:

```sh
cmake -S . -B /tmp/wirelink-dev-tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON -DWIRELINK_WLC_EXECUTABLE="$WLC"
cmake --build /tmp/wirelink-dev-tutorials --parallel
ctest --test-dir /tmp/wirelink-dev-tutorials --output-on-failure
```

This adds the telemetry, RPC, owned-result, device-service and composed-service
examples, generated endpoint fixtures and source-listing checks. Generated C
is under the build tree, in `wirelink-generated/<target>` or the fixture's
generated directory. A profile-v2 `wirelink_wlc_generate` target contains codec,
route/storage and its selected connector; `wirelink_wlc_generate_codec` emits
standalone public codec without an execution profile.

For compiler changes, run from the Wirelink root with `WLC_SOURCE` set:

```sh
cargo test --locked --manifest-path "$WLC_SOURCE/Cargo.toml"
cargo clippy --manifest-path "$WLC_SOURCE/Cargo.toml" \
  --all-targets --all-features -- -D warnings
cargo fmt --manifest-path "$WLC_SOURCE/Cargo.toml" --check
```

`tests/kernel.rs` covers generated routing/storage/profile validation;
`tests/codec_plan.rs`, `tests/bounded_lengths.rs`, `tests/narrow_integers.rs`,
`tests/required.rs`, `tests/owned_values.rs` and `tests/positional.rs` cover codec
contracts. Keep scalar full domains, unknown enum values, float bits and
byte-counted UTF-8 bounds intact. Use [packet/payload reference](packet-protocol.md)
to distinguish public Schema-v1, ordinary Positional-v1 and managed tagged RPC.

## Build the host owner, Session and native SDKs

From the Wirelink root, with `WLC` set, provide standalone Asio headers:

```sh
export ASIO_INCLUDE=/absolute/path/to/asio/include
export PREFIX=/tmp/wirelink-dev-prefix
cmake -S . -B /tmp/wirelink-dev-host -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_WLC_EXECUTABLE="$WLC" -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_BUILD_PLATFORM=ON -DWIRELINK_BUILD_CPP_BINDINGS=ON \
  -DWIRELINK_BUILD_OWNER_PASS_TESTS=ON \
  -DWIRELINK_ASIO_INCLUDE_DIR="$ASIO_INCLUDE" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib
cmake --build /tmp/wirelink-dev-host --parallel
ctest --test-dir /tmp/wirelink-dev-host --output-on-failure
cmake --install /tmp/wirelink-dev-host
```

`WIRELINK_BUILD_CPP_BINDINGS` enables host, platform and Asio UDP dependencies.
Expect Executor, Session, UDP peer and deterministic owner-pass tests as well
as the generated tutorials. UDP tests need permission to open local loopback
sockets. For the owner without Session/Asio, use
`-DWIRELINK_BUILD_HOST_RUNTIME=ON` instead. The external command queue and
kernel admission have different capacities; see [owner execution](kernel.md).

The two SDKs include generated C and can build without running WLC. With the
above installed `PREFIX`, build their native C++ tests from the Wirelink root:

```sh
cmake -S examples/06_bindings -B /tmp/wirelink-dev-calculator \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX"
cmake --build /tmp/wirelink-dev-calculator --parallel
ctest --test-dir /tmp/wirelink-dev-calculator --output-on-failure
cmake -S examples/07_device_sdk -B /tmp/wirelink-dev-device \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX"
cmake --build /tmp/wirelink-dev-device --parallel
ctest --test-dir /tmp/wirelink-dev-device --output-on-failure
```

They verify manifests and exercise SDK calls against independent C peers.
Python extension/wheel prerequisites and commands are in the
[SDK guide](../examples/06_bindings/GUIDE.md) and
[package checks](../tests/package/README.md). Run
`python3 tests/package/check_generated_sdks.py --wlc "$WLC"` after changing
SDK templates or checked-in generated artifacts; regenerate both SDKs, including
their provenance manifests, rather than editing generated files individually.

## Verify an installed consumer

Use a fresh prefix so old headers cannot satisfy a package check. For the
commands below, install the host configuration above first. **Set the install
prefix at configure time:** `wirelink.pc` embeds `CMAKE_INSTALL_PREFIX`.
Overriding only `cmake --install --prefix` relocates CMake exports but does not
rewrite that pkg-config prefix. The host command also fixes `lib` for the path
used below; with another libdir, adjust `PKG_CONFIG_PATH`.

```sh
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" cmake -S tests/package/c_core \
  -B /tmp/wirelink-dev-installed-c -DCMAKE_PREFIX_PATH="$PREFIX"
cmake --build /tmp/wirelink-dev-installed-c --parallel
ctest --test-dir /tmp/wirelink-dev-installed-c --output-on-failure
cmake -S tests/package/consumer -B /tmp/wirelink-dev-installed-consumer \
  -DCMAKE_PREFIX_PATH="$PREFIX" -DWIRELINK_CONSUMER_HOST=ON
cmake --build /tmp/wirelink-dev-installed-consumer --parallel
ctest --test-dir /tmp/wirelink-dev-installed-consumer --output-on-failure
cmake -S tests/package/wlc_current -B /tmp/wirelink-dev-installed-generator \
  -DCMAKE_PREFIX_PATH="$PREFIX" -DWIRELINK_WLC_EXECUTABLE="$WLC"
cmake --build /tmp/wirelink-dev-installed-generator --parallel
ctest --test-dir /tmp/wirelink-dev-installed-generator --output-on-failure
```

The first project enables C only and checks CMake plus pkg-config. The second
checks public C/C++20 headers and the installed owner component. The third
generates native/COBS/C++ endpoints and checks generator diagnostics against
installed files. Optional installed adapters and SDK consumers have their own
projects under `tests/package`; enable/install the needed component first.

## Check memory and undefined behavior

Run from the Wirelink root with Clang and its ASan/UBSan runtimes installed.
Use a separate Debug tree so instrumentation is applied to C and C++ alike:

```sh
cmake -S . -B /tmp/wirelink-dev-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DWIRELINK_BUILD_HOST_RUNTIME=ON
cmake --build /tmp/wirelink-dev-sanitize --parallel
ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
ctest --test-dir /tmp/wirelink-dev-sanitize --output-on-failure \
  -R '^(kernel|kernel_wire|packet_port|packet_port_concurrent|packet_stream|byte_fifo|wirelink_bare_metal_loopback|wirelink_host_executor|wirelink_coalescing_event|wirelink_host_profile)$'
```

Expect no sanitizer diagnostics and all selected cases to pass. LeakSanitizer
needs a runtime environment that permits its thread inspection; a ptrace/sandbox
failure is an environment failure, not a passing memory check. Run the same
instrumented binaries in an environment with that capability. Add the
host/Session/WLC options above when checking their generated/UDP paths under
sanitizers. Concurrent fixtures keep atomic storage and accesses in C; C++
creates/joins threads. Do not replace C atomics to make instrumentation compile.

For fuzzing, `-DWIRELINK_BUILD_FUZZERS=ON` requires Clang/libFuzzer and matching
WLC. Targets and smoke tests are defined in [`fuzz/CMakeLists.txt`](../fuzz/CMakeLists.txt).

## Run Zephyr checks

Use an initialized Zephyr workspace with its Python/west environment active,
the toolchains for the selected platforms, and the Zephyr revision declared in
[`west.yml`](../west.yml). Register Wirelink as a module; the test projects also
add their repository as `ZEPHYR_EXTRA_MODULES`. No board is opened by these
controlled driver tests. From that Zephyr workspace:

```sh
export WIRELINK_SOURCE=/absolute/path/to/wirelink
west twister -T "$WIRELINK_SOURCE/tests/zephyr/unit/static_packet_port" \
  -T "$WIRELINK_SOURCE/tests/zephyr/unit/static_stream" \
  -T "$WIRELINK_SOURCE/tests/zephyr/integration/static_uart" \
  -T "$WIRELINK_SOURCE/tests/zephyr/integration/static_usb_bulk" \
  -T "$WIRELINK_SOURCE/tests/zephyr/integration/static_udp" \
  -p unit_testing -p native_sim --outdir /tmp/wirelink-dev-zephyr \
  --inline-logs -j 4
```

`static_packet_port` reuses the kernel/port/wire C fixtures; `static_stream`
checks the connectors. `static_uart` exercises IRQ/DMA/poll driver boundaries,
`static_usb_bulk` USBD completion/reset, and `static_udp` native socket contracts.
Expect each selected configuration to run and pass; inspect Twister's JSON/XML
for filtered, skipped, build-only and executed counts separately.

For the wider unit/integration set, from the same workspace with `WLC` set:

```sh
west twister -T "$WIRELINK_SOURCE/tests/zephyr/unit" \
  -T "$WIRELINK_SOURCE/tests/zephyr/integration" \
  -p unit_testing -p native_sim -p qemu_cortex_m3 -p qemu_riscv32 -p qemu_x86_64 \
  -x=WIRELINK_WLC_EXECUTABLE="$WLC" \
  --outdir /tmp/wirelink-dev-zephyr-all --inline-logs
```

Platform allow lists determine which suites run on each target. Native tests
need their host build dependencies; QEMU targets need their SDK/emulator.
Board builds and real UART/USB captures use the sample/platform instructions and
the qualified driver patches listed in [Zephyr CI](../.github/workflows/zephyr.yml).
Check an external tree's existing changes before applying a patch. A sample
build or controlled driver test does not establish physical line behavior; use
the [adapter HIL checklist](adapter-hil.md) for that work.

## Map a change to decisive checks

CTest names below refer to the core or host build trees above; Twister paths
are relative to the repository. Each check must preserve both success and
failure behavior at the changed boundary.

| Change | Checks and required evidence |
| --- | --- |
| Deadline, budget, cancellation or callback reentry | `ctest -R '^kernel$'`; equality/wrap, simultaneous expiry, budgets `1..16`, callback-created calls, one terminal and RX/TX progress |
| Token identity, RX publication or TX lifetime | `packet_port`, `packet_port_concurrent`; stale/duplicate completions, inline completion, stop/reinit and source lifetime |
| DATA/RPC metadata | `kernel_wire`; retain [`rpc_vectors.h`](../tests/core/rpc_vectors.h), reject malformed metadata and unsupported packet kinds |
| Stream encoding or staging | `packet_stream`, `byte_fifo`; all payload lengths, independent frame oracle, partial feed, pressure versus gap recovery |
| Typed routes, profile validation or codec bounds | WLC `--test kernel` and relevant codec tests; `generated_usb`, `generated_instrument`, `generated_loopback`, `kernel_cpp`; repeatable generation and exact business values |
| Owner wake/stop or external commands | `wirelink_host_executor`, `wirelink_coalescing_event`, `wirelink_owner_pass`; latched waits, command pressure and stop/join |
| Session or SDK ownership | `wirelink_cpp_session`, native SDK CTests, relevant Python/wheel tests; results retained after close and admission/terminal errors kept separate |
| UART completion or RX recovery | `tests/zephyr/integration/static_uart`; buffer return, physical drain, BUSY wake and callback stop; actual UART capture for hardware claims |
| USB boundary/reset or UDP waiting | `static_usb_bulk` / `static_udp`; host `kernel_packet_udp` / controlled adapter tests as applicable; headroom, stale tokens, real reset and shutdown |
| Installation, public headers or generation helper | Installed `c_core`, `consumer`, `wlc_current`, plus affected optional consumer; no source-tree includes or missing target dependencies |

For example, after a deadline edit:

```sh
cmake --build /tmp/wirelink-dev-core --parallel
ctest --test-dir /tmp/wirelink-dev-core --output-on-failure -R '^kernel$'
```

## Diagnose the boundary before changing behavior

- **Admission fails:** distinguish `WOULD_BLOCK` from encoding, closed-state and
  identity-exhaustion errors. No admitted call means no terminal callback.
- **No progress after BUSY:** confirm actual resource activity reaches the port,
  then check adapter service/readiness and the latched wait. Repeated steps
  without a resource change are not a recovery strategy.
- **Handler effects but no result:** inspect reply reservation/encoding and
  client deadline. Reply-encoding failure is a local peer error, not an automatic
  business rejection. Preserve request IDs when diagnosing duplicate effects.
- **Timeout with a busy transmitter:** inspect both call identity and physical
  TX token. A terminal can free a logical call while leaving snapshot bytes
  leased. Keep stepping and wait for actual adapter completion/stop.
- **Close never finishes:** inspect active claims, queued/borrowed RX, untaken
  TX completion and driver callbacks/worker exit separately. Do not force
  quiescence by clearing state or manufacturing DRAINED.
- **Wire mismatch:** identify connector, message ID and selected payload codec
  before decoding fields; scalar `fixed32` differs between Schema-v1 and managed
  tagged bodies. Preserve a failing byte vector.

## Keep changes reviewable

Use two-space C indentation, same-line braces, `snake_case`, `wl_` public symbols,
`WL_` constants and SPDX headers. Keep C warning-clean and format Rust with
`cargo fmt`. Preserve deterministic generated output, schema field/message IDs,
scalar domains and frozen conformance vectors. A capacity or codec-domain
change needs an explicit contract decision, not adjusted documentation to hide
a failing value.

Test errors as well as success: snapshot before return, reservation before
effects, callback borrowing, stale identities, separate consumption/drain and
real stop/join. Update direct callers, fixtures and docs together. Use scoped
commits with behavior/compatibility impact and the actual commands/platforms
tested. For measurement changes, define the measured work and retain build
configuration/results; [benchmarks](../benchmarks/README.md) supplies the entry
points. Keep hardware evidence distinct from software models.
