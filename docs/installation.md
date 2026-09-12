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

This tree requires **WLC 0.8.0 with codegen ABI 32**. This release
revision adds static imports and borrowed direct routes without changing the
ABI 31 wire formats. Published v0.6.0 / ABI 31 binaries cannot generate this API.
CMake fetches the matching host package automatically. To build it yourself,
check out the independent WLC repository at tag `v0.8.0` and run:

```sh
cargo build --release --locked
./target/release/wlc codegen-abi
```

Pass the absolute path to `target/release/wlc` (`wlc.exe` on Windows) to CMake;
this does not replace your installed compiler. The checkout can live anywhere.
The exact source commit and archive checksum are pinned in CMake.

On Windows, preserve LF when cloning the tagged compiler source:
`git -c core.autocrlf=false clone --branch v0.8.0 https://github.com/starwey604/wlc.git`.
WLC embeds SDK template bytes at build time; automatic CRLF conversion changes
generated artifacts. The published source archive and binaries use canonical LF.

## 3. Verify installation

```sh
wlc --version
wlc codegen-abi
```

Expect `wlc 0.8.0` and `32` from the executable you will pass to CMake.
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

CMake selects a package using the build host, not the embedded target: Windows
x86-64, Linux x86-64/aarch64 (static musl), or macOS x86-64/arm64. It downloads
the pinned v0.8.0 archive, verifies its hard-coded SHA256, and checks both
compiler version and ABI. No Rust toolchain or local WLC checkout is needed.
Other hosts use the paired, verified source archive and host Rust/Cargo.

For offline builds, supply `-DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc`
and `-DWIRELINK_WLC_AUTO_DOWNLOAD=OFF`. No consumer needs our worktree layout.

Continue with [displaying temperature](getting-started.md).
