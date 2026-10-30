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

This development tree requires **WLC 0.12.0, codegen contract 0.12**. Build the
matching independent WLC working tree supplied with this iteration (its path
need not be nested in Wirelink):

```sh
cargo build --release --locked --manifest-path /path/to/matching-wlc/Cargo.toml
/path/to/matching-wlc/target/release/wlc --version
/path/to/matching-wlc/target/release/wlc codegen-contract
```

Use that absolute executable path as `WIRELINK_WLC_EXECUTABLE`, with
`WIRELINK_WLC_AUTO_DOWNLOAD=OFF`. Preserve LF when cloning/building the compiler;
its embedded SDK templates must be deterministic. The round-1 report records the
exact working-tree revisions and hashes used for verification. Published WLC
0.8.0 cannot generate the new execution interfaces.

## 3. Verify installation

```sh
wlc --version
wlc codegen-contract
```

Expect `wlc 0.12.0` and `0.12` from the executable you will pass to CMake.
The codegen contract identifies generated C interfaces and layouts,
not the wire protocol. Managed and mapped RPC require different payload formats;
switching modes or managed metadata versions needs coordinated peers;
rebuild core and generated consumers together. A missing command or
different contract means a different compiler build is needed. CMake checks both
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

No 0.12 release archive/source pair is pinned yet. Automatic download therefore
fails explicitly rather than using the old published 0.8 compiler. Supply the
matching local executable as described above. Historical published workflows
are documented in the [0.8 release guide](release-v0.8.0-cn.md).

Continue with [displaying temperature](getting-started.md).

Regeneration and removed interfaces are covered by [0.12](migration-0.12.md).
