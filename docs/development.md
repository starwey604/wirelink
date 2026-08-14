# Development policy

## WLC compiler baseline

The Rust compiler lives in `wlc/`. Its initial frontend owns the versioned
`.wl` grammar, AST, and source-located validation. The supported baseline
constructs are `message`, `enum`, `optional`, `repeated`, and `default`; see
`wlc/README.md` for the normative grammar and current validation rules.

Run its focused checks from the repository root with:

```sh
cargo test --manifest-path wlc/Cargo.toml
```

The frontend accepts user-defined field types syntactically. Cross-reference
resolution, stable ID allocation, compatibility checks, IR, and C generation
belong to the subsequent WLC milestones and must preserve this grammar.

`wlc` uses `miette` for source-aware user diagnostics, `thiserror` for typed
library errors, `clap` for its CLI, and `insta` for future reviewed codegen
snapshots. New dependencies need a focused compiler concern and must be
recorded in `wlc/README.md` before adoption.

## WLC semantic baseline

`analyze_schema` resolves parsed field types and produces an ID-sorted semantic
model. ID allocation is explicit: schemas must never receive implicit message,
enum, or field numbers. `check_compatibility` compares two such models; deleted
declaration IDs require top-level `reserved N;`, while deleted fields require
the same declaration inside their message. Reserved IDs are permanent.

The baseline treats a field's number, name, resolved type, and cardinality as
wire identity. Reordering source declarations or fields is semantically inert.
CLI support for selecting a previous schema is deferred to the CLI milestone;
the library API and tests are the compatibility contract until then.

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
