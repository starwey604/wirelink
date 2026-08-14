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

The top-level target is compiled as strict ISO C11. GCC and Clang builds enable
`-Wall`, `-Wextra`, and `-Wpedantic`; warnings in Wirelink sources are treated
as release blockers even when a consumer supplies different project defaults.

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

The RX ring backend is selected at build time and does not enter the public
ABI. Top-level CMake accepts `-DWIRELINK_RX_BACKEND=BIPBUF` (the default) or
`LWRB`; Zephyr uses the matching `CONFIG_WIRELINK_RX_BACKEND_*` choice. The
vendored LwRB backend retains its C11 atomic indices and requires them to be
lock-free on the target.

## Core storage contract

The v1 core has one application TX slot, one control/ACK TX slot, and one RX
event slot. `wl_init()` receives a `wl_storage_t`; its buffers remain owned by
the caller and must outlive the context. Use `wl_config_requirements()` before
allocation to obtain the exact worst-case buffer sizes for the configured
profile. A profile's `max_transmission_unit`, when nonzero, must accommodate
the complete envelope including COBS delimiters or a length prefix.

Application bytes are copied before a send is accepted and are encoded into
the caller-supplied TX unit buffer. The pointer given to a `WL_SINK_STARTED`
callback remains valid until its matching `wl_tx_complete()`. ACK/control
units use their own buffer and are submitted before a queued application unit.

COBS stream bytes enter a single-producer/single-consumer RX ring. The producer
side (`wl_feed_bytes()` or `wl_rx_reserve()`/`wl_rx_commit()`) only publishes
bytes; COBS decoding, frame validation, ACK scheduling, and event creation run
from the consumer-side `wl_poll()`. A contiguous frame is decoded in place and
its event payload borrows the ring. A frame crossing the ring boundary is
copied once into the caller-supplied fallback buffer. Native packet and bus
units also use that fallback buffer to make their synchronous input lifetime
explicit.

An RX event remains leased until `wl_event_release()`. A ring-backed event
freezes its physical frame bytes until release, and no subsequent DATA event is
produced while any RX event is leased. The SPSC contract permits exactly one
ISR or DMA-completion producer and one main-loop consumer; initialization,
reset, polling, and event release must not run from additional concurrent
contexts. Malformed, integrity-failing, overflowed, duplicate, and unsupported
packets are observable through the RX counter query rather than as application
events.

Reliable TX handles contain a slot-generation value. A terminal reliable
transaction remains queryable until `wl_tx_take()` returns its result, after
which the old handle is invalid. The ACK timer starts only once the local sink
reports completion; retries are counted separately from the initial send.
Reliable RX deduplicates the most recent `(session_id, sequence)` and re-ACKs
duplicates without emitting another event.

## Test layers

- `tests/zephyr/unit/`: Twister `type: unit` tests using the host-native
  `unit_testing` board. Use for pure C modules such as BipBuffer, CRC, COBS,
  frame codecs, and ARQ state transitions.
- `tests/zephyr/integration/`: Ztest applications run on `native_sim`, QEMU,
  or hardware. Use for protocol-stack and adapter integration.
- Multi-device transport tests use Twister's pytest harness and a hardware map
  when real DUTs are required.

`tests/zephyr/integration/protocol` is the core in-memory two-peer fixture. It
exercises reliable acknowledgement, a dropped first DATA retry, COBS
byte-at-a-time ingestion, and corrupted-unit rejection without hardware.

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
