# Build tools and installation

[中文](installation-cn.md).

The portable core needs ISO C11 and CMake 3.21+. C++20 is needed for host
Executor, C++ Session and desktop adapters. MSVC C sources use
`/experimental:c11atomics`; C owns atomic storage and C++ never assumes a
`std::atomic` ABI. Python 3.11+ runs the benchmark tooling; tests may require
Python as declared by their CMake targets.

The default core and checked-in bare-metal example need no WLC:

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/absolute/prefix -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
cmake --install build/core
```

Generation requires **WLC 0.13.0 / codegen 0.13**. Download the archive for your
platform from [WLC v0.13.0](https://github.com/starwey604/wlc/releases/tag/v0.13.0),
verify it against the release's `SHA256SUMS`, and extract `wlc` (`wlc.exe` on
Windows). The five archives cover Linux musl x86-64/ARM64, Windows x86-64 and
macOS Intel/ARM. The archive's `wlc-build.json` records its exact source pair.

Alternatively, build the matching independent compiler source. Rust/Cargo is
needed only for this source build, not for using an archived compiler or the C
core. Preserve LF in compiler templates.

```sh
cargo build --release --locked --manifest-path /path/to/matching-wlc/Cargo.toml
/path/to/matching-wlc/target/release/wlc --version
/path/to/matching-wlc/target/release/wlc codegen-contract
```

Expect `wlc 0.13.0` and `0.13`. Set `WIRELINK_WLC_EXECUTABLE` to that absolute
path. The helper checks compiler and generated manifest before compilation;
it uses the selected local tool.
Codegen version describes interfaces/layout, not transport wire version.

```sh
cmake -S . -B build/tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc
cmake --build build/tutorials --parallel
ctest --test-dir build/tutorials --output-on-failure
```

Use `find_package(Wirelink 0.13.0 EXACT CONFIG REQUIRED)` with
`CMAKE_PREFIX_PATH=/absolute/prefix` for installed consumers. The base C package
requires no C++ compiler. Optional installed host/adapter components must have
been built and require CXX in the consuming project.

Asio UDP requires standalone Asio headers; the locally qualified source is
1.38.1, commit `bbecff21a23b97c34641f0f1f08b28c91b9c77cf`. Set
`WIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio/asio/include` and enable
`WIRELINK_BUILD_ASIO_UDP_ADAPTER=ON`, plus `WIRELINK_BUILD_PLATFORM=ON` for the
UDP example. Astrial adapters use `WIRELINK_ASTRIAL_SOURCE_DIR`; native USB also
requires the backend's libusb dependency. These remain outside the C core.

Continue with [telemetry](getting-started.md), [the kernel model](kernel.md) or
the [development guide](development.md). Installed CMake/pkg-config consumers
are demonstrated in [package checks](development.md#verify-an-installed-consumer).
