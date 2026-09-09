# Environment setup: WLC and host UDP dependencies

WLC is a separate message compiler. It runs on your computer, converts `.wl`
definitions into C, and those files are compiled into applications or firmware.
Wirelink does not contain or require a nested WLC checkout. [中文](installation-cn.md).

## 1. Build tools

The tutorials require C11 and C++20 compilers and CMake 3.21 or newer. Business
examples are C11; only desktop Asio support needs C++20. Windows can use a recent
Visual Studio/MSVC with C11 support. Tutorial builds disable tests by default;
Python 3 is only required when enabling automated process tests.
A prebuilt WLC needs no Rust installation; Rust/Cargo is needed only to build WLC.

## 2. Get a matching compiler

This tree requires **WLC 0.5.0 with codegen ABI 30**, adding composed
profiles, profile-selected endpoint layouts and private validated RPC paths. ABI 30 preserves
ABI 26 wire formats. The v0.5.0 release gives this API its own
compiler version: published v0.4.0 is ABI 12 and cannot generate this API.
Download your platform's archive and `SHA256SUMS` from the
[WLC v0.5.0 release](https://github.com/starwey604/wlc/releases/tag/v0.5.0).
Verify the archive's SHA-256, then extract it to a stable location and
add its executable directory to `PATH`. Alternatively pass
`-DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc` when configuring Wirelink.

Without a matching binary, first obtain the matching **WLC v0.5.0 source**,
then build in that independent checkout:

```sh
cargo build --release --locked
./target/release/wlc codegen-abi
```

Pass the absolute path to `target/release/wlc` (`wlc.exe` on Windows) to CMake;
this does not replace your installed compiler. The checkout can live anywhere.
The matching source commit is `120b9af130753d2ba0d137882916bfe207d3d312`
(`v0.5.0`). Use this tag/commit rather than a moving branch.

## 3. Verify installation

```sh
wlc --version
wlc codegen-abi
```

Expect `wlc 0.5.0` and `30` from the executable you will pass to CMake.
Codegen ABI identifies generated C interfaces/layouts,
not the wire protocol. Managed and mapped RPC require different payload formats;
switching modes or managed metadata versions needs coordinated peers;
rebuild core and generated consumers together. A missing command or
different ABI means a different compiler build is needed. CMake checks both
values before generation rather than leaving a header mismatch for firmware compilation.

## 4. Obtain standalone Asio

Asio is an optional cross-platform C++ networking dependency, not a core/firmware
dependency. Obtain the tested version anywhere outside or inside your own project:

```sh
git clone --branch asio-1-38-1 --depth 1 https://github.com/chriskohlhoff/asio.git asio-source
```

The corresponding commit is `bbecff21a23b97c34641f0f1f08b28c91b9c77cf`.
Set `WIRELINK_ASIO_INCLUDE_DIR` to the absolute `asio-source/asio/include`
directory containing `asio.hpp`, or use matching installed headers.
Tutorial builds enable the adapter; programs bind only localhost and need no
virtual serial driver.

## 5. Automatic downloads

If no matching compiler is supplied explicitly or on PATH, CMake downloads the
pinned v0.5.0 source, verifies its SHA-256 and builds a host executable with
Rust/Cargo. It caches the result under `WIRELINK_WLC_CACHE_DIR`; a firmware
`CARGO_BUILD_TARGET` does not change the host tool's target. First use needs
network access and a Rust 2024 toolchain. For offline builds, supply the compiler
and set `WIRELINK_WLC_AUTO_DOWNLOAD=OFF`. No consumer needs our worktree layout.

Continue with [displaying temperature](getting-started.md).
