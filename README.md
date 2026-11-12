# Wirelink

Wirelink is an allocation-free C11 kernel for typed messages and RPC over
point-to-point links. Your application supplies storage, one owner, monotonic
time and a transport. Generated routes, host wrappers and SDKs share the same
kernel; retries, idempotency and long-running jobs belong to the application.

## Run an endpoint

With a C11 compiler and CMake 3.21+, run from this repository:

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
./build/core/examples/wirelink_bare_metal_loopback
```

The example uses checked-in generated C, without WLC or hardware. It prints:

```text
8 bidirectional RPC completions, 2 telemetry messages; both endpoints quiescent
```

To generate your own messages, [set up matching tools](docs/installation.md)
([中文](docs/installation-cn.md)): Wirelink **0.13.0**, WLC **0.13.0** / contract
**0.13**, SDK binding source API **3**. Then follow the build-backed tutorials:
[telemetry](docs/getting-started.md), [RPC](docs/tutorial-rpc.md),
[retained results](docs/tutorial-rpc-values.md),
[cancellation and capacity](docs/tutorial-rpc-async.md),
[application jobs](docs/tutorial-jobs.md).

## Understand the kernel

Start with [how the kernel works](docs/kernel.md) ([中文](docs/kernel-cn.md)):
message/RPC flow, owner execution, capacities, waiting and shutdown. Use the
[kernel and packet-port API](docs/kernel-api.md) ([中文](docs/kernel-api-cn.md))
for signatures, admission errors and buffer lifetimes.

For a deployment, read [profiles and capacity budgets](docs/link-profiles.md)
and the [packet/RPC wire format](docs/packet-protocol.md). Peers agree on schema,
message IDs and connector: native/NONE, COBS/CRC16 or length16/NONE.

## Develop and verify

The [development guide](docs/development.md) ([中文](docs/development-cn.md))
maps each kind of change to its source and decisive checks. It includes core,
generated tutorials, installed packages, host/SDK, sanitizer and Zephyr commands.
Use the [benchmark guide](benchmarks/README.md) for measurement workloads and records.

## Integrate as needed

- **Your transport:** [one owner loop](docs/tutorial-integration.md) and
  [adapter contracts](docs/adapters.md) ([中文](docs/adapters-cn.md)) cover UART,
  native USB bulk and UDP, including physical completion and recovery.
- **Desktop clients:** [host owner and Session](docs/host-runtime.md),
  [C++/Python Calculator SDK](examples/06_bindings/GUIDE.md), and
  [owned Device SDK values](examples/07_device_sdk/README.md).
- **Zephyr:** [samples](samples/zephyr/) and
  [UDP peers](samples/zephyr/udp_peer/README.md).
- **Existing build:** [installation and CMake](docs/installation.md);
  `Wirelink::wirelink` links the C kernel, `wirelink.pc` serves C pkg-config users.
- **Local storage:** independent [FIFO](docs/fifo.md),
  [LATEST mailbox](docs/latest-mailbox.md) and optional fixed-pool storage;
  application code chooses their integration with the kernel.

[Documentation website](https://docs.silkenkite.ink/wirelink/) ·
[WLC compiler](https://github.com/starwey604/wlc) ·
[Apache-2.0 license](LICENSE)
