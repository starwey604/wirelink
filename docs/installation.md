# Environment setup: install WLC

WLC is a separate message compiler. It runs on your computer, converts `.wl`
definitions into C, and those files are compiled into applications or firmware.
Wirelink does not contain or require a nested WLC checkout. [中文](installation-cn.md).

## 1. Build tools

The Linux/macOS tutorial commands require a C11 compiler and CMake 3.21 or newer.
A prebuilt WLC needs no Rust installation; Rust/Cargo is needed only to build WLC.

## 2. Get a matching compiler

The tutorials' ID attributes and managed RPC require **WLC 0.4.0 with codegen ABI 20**, an internal
development build. An older release with the same package version is not enough.
No new package or tag is published by this iteration.

If supplied with a matching internal binary, extract it to a stable location and
add its executable directory to `PATH`. Alternatively pass
`-DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc` when configuring Wirelink.

Without a matching binary, independently obtain and install the WLC source:

```sh
git clone --branch dev/wirelink-p0-hardening https://github.com/starwey604/wlc.git wlc-source
git -C wlc-source checkout 6c992decc4b200d258bd8c7409a8896ab37a17e8
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

Expect `wlc 0.4.0` and `20`. Codegen ABI identifies generated C interfaces/layouts,
not the wire protocol. Managed RPC uses a new RPC payload format and requires
matching peers; existing mapped RPC and business codecs keep their bytes. A missing command or
different ABI means a different compiler build is needed. CMake checks both
values before generation rather than leaving a header mismatch for firmware compilation.

## 4. Automatic downloads

Wirelink's CMake integration can download fixed WLC GitHub release assets and
verify their digests. This internal ABI does not assume a matching public release
asset, so the tutorial disables automatic download and uses the installed tool.
When matching packages are distributed, Wirelink should pin source, platform
asset names, and hashes. Users should never need our worktree layout.

Continue with [displaying temperature](getting-started.md).
