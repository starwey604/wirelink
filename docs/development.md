# Development policy

## Persistent testing decision

Wirelink uses Zephyr's Twister and Ztest as its only maintained test
framework. This includes tests of the platform-independent C core: they run as
Ztest `unit_testing` scenarios rather than as a parallel CTest suite.

The top-level CMake build remains supported for library consumers, but it does
not define or run tests.

## Zephyr architecture

`src/` and `include/wirelink/` are the transport- and RTOS-independent C
protocol core. They must not depend on Zephyr kernel APIs, devicetree, or
drivers. The repository is a Zephyr module through `zephyr/module.yml`:

- `zephyr/Kconfig` controls whether the core is included (`CONFIG_WIRELINK`).
- `zephyr/CMakeLists.txt` builds the existing C sources as a Zephyr library.
- Future UART, USB, SPI, and UDP adapters belong in separate Zephyr-specific
  sources and depend on a transport interface owned by the core.

This keeps the same protocol implementation usable in bare-metal, other RTOS,
and desktop applications.

## Test layers

- `tests/zephyr/unit/`: Twister `type: unit` tests using the host-native
  `unit_testing` board. Use for pure C modules such as BipBuffer, CRC, COBS,
  frame codecs, and ARQ state transitions.
- `tests/zephyr/integration/`: Ztest applications run on `native_sim`, QEMU,
  or hardware. Use for protocol-stack and adapter integration.
- Multi-device transport tests use Twister's pytest harness and a hardware map
  when real DUTs are required.

## Integration platform matrix

Use `native_sim` as the default runnable integration target: it is fast and is
the appropriate host environment for virtual transport pairs and UDP tests.
For build-and-run coverage on emulated CPU architectures, use this initial
matrix:

| Platform | Purpose |
| --- | --- |
| `native_sim` | Default host integration test and virtual transport peer. |
| `qemu_cortex_m3` | 32-bit Cortex-M embedded ABI and constrained-RAM checks. |
| `qemu_riscv32` | 32-bit RISC-V toolchain and architecture coverage. |
| `qemu_x86_64` | 64-bit QEMU coverage independent of the native host binary. |

Only integration tests should list these platforms in `tests.yaml`; pure-core
tests remain `type: unit` and run exclusively on `unit_testing`. Hardware
scenarios are added separately with explicit driver and fixture requirements.

From an existing Zephyr workspace, run the current BipBuffer unit test with:

```sh
cd ~/zephyrproject/zephyr
../.venv/bin/west twister -T /path/to/wirelink/tests/zephyr/unit/bipbuf \
  -p unit_testing
```

For a standalone Wirelink workspace, initialize this repository as a west
manifest repository and run `west update`; `west.yml` pins the Zephyr release
and imports its standard dependency manifest.
