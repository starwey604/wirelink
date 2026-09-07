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

Use the pinned **WLC 0.4.0 with codegen ABI 24** revision below, including
initialization-time endpoint clocks, `@delivery(...)`, and default RPC reliability.
ABI 24 changes generated C APIs/layouts, not schema encoding or Wirelink frame bytes.
No new package or tag is published by this iteration.

If supplied with a matching internal binary, extract it to a stable location and
add its executable directory to `PATH`. Alternatively pass
`-DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc` when configuring Wirelink.

Without a matching binary, independently obtain and install the WLC source:

```sh
git clone --branch dev/wirelink-p0-hardening https://github.com/starwey604/wlc.git wlc-source
git -C wlc-source checkout 26a07a49597cd06b455ea1060e5b7902d39ea061
cargo install --path wlc-source --locked --force
```

Cargo installs `wlc` in its binary directory; ensure that directory is on `PATH`.
`--force` replaces an existing WLC there. Use Cargo's `--root` for a separate
installation and pass its executable explicitly if versions must coexist.
The source directory can be anywhere, independent of Wirelink. Record the exact
WLC commit for reproducible development builds instead of tracking a moving branch.

## 3. Verify installation

```sh
wlc --version
wlc codegen-abi
```

Expect `wlc 0.4.0` and `23`. Codegen ABI identifies generated C interfaces/layouts,
not the wire protocol. Managed and mapped RPC require different payload formats;
switching modes needs coordinated peers. Clock injection does not change bytes;
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

Wirelink's CMake integration can download fixed WLC GitHub release assets and
verify their digests. This internal ABI does not assume a matching public release
asset, so the tutorial disables automatic download and uses the installed tool.
When matching packages are distributed, Wirelink should pin source, platform
asset names, and hashes. Users should never need our worktree layout.

Continue with [displaying temperature](getting-started.md).
