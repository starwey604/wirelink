# Wirelink

[简体中文](README.md) · [Documentation](docs/README-en.md) · [Compiler reference (中文)](wlc/README.md)

Wirelink is an allocation-free ISO C11 communication library for embedded MCUs and host tools over UART, USB Bulk, UDP, or SPI. One `.wl` file declares messages, RPCs, and node roles; WLC generates one C header/source pair. Nodes default to embedded I/O and static handlers. Fields are required by default; only explicit `optional` fields carry presence flags.

The local 0.14.0 candidate requires **WLC 0.14.0 / codegen contract 0.14 / SDK Binding API 3**. Regenerate C and SDK artifacts when migrating from 0.13; see the [migration notes (中文)](docs/release-v0.14.0-cn.md).

## Three steps

1. Declare messages, `rpc Add(AddRequest) -> AddResponse;`, and node roles such as `node client { call Add; }` / `node server { serve Add; }` in one file. See [calculator.wl](examples/01_rpc/calculator.wl).
2. Generate the role you deploy:

   ```sh
   wlc compile examples/01_rpc/calculator.wl --node server --out-dir build/server
   # Use --all-nodes for a process hosting both roles.
   wlc sdk examples/01_rpc/calculator.wl --node client --name calculator --out-dir build/sdk
   ```

   No nodes means codec-only output. One node is selected automatically; multiple nodes require `--node` or `--all-nodes` for C generation.
3. Implement `server_on_add()` and the whole-frame synchronous `server_tx_write()` hook, call `server_init(&server)`, deliver packets through `server_rx_packet(&server, bytes, length)`, and advance `server_step(&server, now)`. See the [complete loopback example](examples/01_rpc/). Notify `server_tx_ready()` after a busy sink becomes writable. Use `io = external` for asynchronous DMA or zero-copy drivers.

Simple RPC calls start their timeout at the next owner step. The `_ex` form accepts explicit time, timeout, callback context, and an optional cancellation handle. Stop the RX producer before closing; step until the node is quiescent before reusing its storage.

## Local build without WLC or hardware

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
./build/core/examples/wirelink_bare_metal_loopback
```

This default build needs C11 and C++20 compilers plus CMake 3.21+. C++20 is used by the C/C++ packet-port ABI tests; the core and checked-in loopback demo remain pure C11. Detailed engineering documentation is in Simplified Chinese: [wire format](docs/wire-format.md), [transport adapters](docs/porting-adapters.md), and [development/testing](docs/development.md).

The measured bidirectional demo uses 790 B (native), 1,054 B (COBS + FIFO), or 932 B (length16) of total assembly RAM, **8 B more** than each 0.13 baseline. These totals include external ports and buffers, not just the node object; see [footprint methodology](benchmarks/footprint/README.md).

This is a local candidate, not a remotely published release. The frozen release-pair file still describes 0.13.

[Apache-2.0 License](LICENSE)
