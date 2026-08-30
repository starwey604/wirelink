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

The RX ring is a fixed, internal atomic SPSC BipBuffer implementation. It does
not enter the public ABI and Wirelink intentionally exposes no build-time
backend selection. The producer release-publishes its write cursor and the
consumer release-publishes reclaimed space; initialization requires the C11
atomics to be lock-free on the target. The selection rationale and ESP32-S3
measurements are retained in `docs/rx-performance.md`.

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
contexts. A producer-side full-ring notification causes the next consumer poll
to discard the incomplete RX window and resume at a later COBS delimiter; it
does not parse or run callbacks from the producer context. Malformed,
integrity-failing, overflowed, duplicate, and unsupported packets are
observable through the RX counter query rather than as application events.

## Direct DMA ingress

`wl_rx_dma_claim()`, `wl_rx_dma_publish()`, `wl_rx_dma_finish()`, and
`wl_rx_dma_abort()` are a platform-independent producer lifecycle for a DMA
engine that writes directly into the RX ring. At most two claims may be active;
they are published and finished strictly in allocation order. A partial publish
only exposes a completed prefix. A last, partially published claim may then be
finished, returning its unwritten tail to the ring; this supports DMA engines
that release their current buffer on an idle timeout. A partially published
claim with a queued successor cannot finish, because removing its tail would
leave a logical hole in the contiguous ring.

The platform adapter, not the core, owns DMA descriptors, cache maintenance,
interrupt registration, and restart policy. It must stop DMA access to every
claim before calling `wl_rx_dma_abort()`. Abort records overflow, and the main
loop must run `wl_poll()` once to discard the incomplete COBS window before
the adapter resumes ingress. This contract maps to Zephyr UART async RX, Linux
DMA/serial drivers, and bare-metal completion ISRs without putting their types
or scheduling assumptions in the core.

`adapters/zephyr/uart_dma/` is the first mapping and is a unified full-duplex
owner of Zephyr's asynchronous UART callback. Enable it with
`CONFIG_WIRELINK_ZEPHYR_UART_DMA=y`. Initialization registers the UART's sole
async callback and installs its `uart_tx()` sink on the Wirelink context, so a
second IRQ or async adapter must not use that UART concurrently. The callback
publishes only new `UART_RX_RDY` bytes and records TX completion in an atomic
mailbox; protocol TX completion, retry, ACK submission, RX reclamation, and
recovery remain in the single-consumer `service()` call.

With `SYS_FOREVER_US` RX timeout the adapter answers `UART_RX_BUF_REQUEST` with
a second direct ring claim for sustained throughput. With a finite timeout it
deliberately runs one DMA buffer at a time, so an idle-released short buffer can
be finished without creating a hole; `service()` re-arms RX after
`UART_RX_DISABLED`. The release bit is the atomic ownership handoff from the
driver callback to `service()`; slot progress fields are not read by the
consumer until that handoff. `WL_ERR_WOULD_BLOCK` from `service()` is transient
and means the main loop should retry on a later iteration.

The lifecycle is `init()` once, `start()`, then `wl_poll()`/event handling and
`service()` from the same main-loop context. `stop()` rejects new TX immediately
and requests RX disable plus TX abort; keep servicing until statistics report
both `started=0` and `stopping=0`, because the driver may still own buffers.
`UART_TX_DONE` and `UART_TX_ABORTED` are deliberately deferred to `service()`:
this keeps ISR work bounded and lets a reliable retry synchronously submit a
new TX only after the previous UART ownership has been released. When the
driver reports DMA FIFO fill rather than final line completion, enable
`wait_for_tx_idle`; a successful DMA event is then held until
`uart_irq_tx_complete()` reports the UART physically idle. This is opt-in
because not every asynchronous driver also implements the interrupt-driven
query. It gives the ACK timer the correct starting boundary on ESP32-S3. The
DMA benchmark runs a startup TX-completion self-test while RX is active before
collecting RX measurements.

`adapters/astrial/` is the optional desktop serial mapping for Linux, Windows,
and macOS applications. It is deliberately outside the C11 core and requires
C++20 plus Astrial. Astrial owns the operating-system serial port and I/O
thread; its borrowed read provider calls `wl_rx_reserve()` on that thread so
Asio reads directly into Wirelink's SPSC ring. The matching completion commits
only the bytes reported by the operating system. An empty reservation pauses
the Astrial read loop, leaving bytes in the OS buffer until the main loop
releases ring space and calls adapter `service()`.

The Astrial TX sink uses borrowed asynchronous writes and returns
`WL_SINK_STARTED`. Its I/O-thread callback only publishes completion into an
atomic mailbox. `service()`, called from the Wirelink consumer context after
`wl_poll()`, releases adapter ownership before calling `wl_tx_complete()` so a
retry or pending ACK may submit synchronously. Thus neither Asio callbacks nor
the Astrial worker thread run the Wirelink protocol state machine.

Build the host adapter against an existing Astrial target, or point Wirelink at
an Astrial source checkout:

```sh
cmake -S . -B build/host \
  -DWIRELINK_BUILD_ASTRIAL_ADAPTER=ON \
  -DWIRELINK_ASTRIAL_SOURCE_DIR=/path/to/astrial \
  -DBUILD_TESTING=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

`SerialAdapter::open()` owns the Astrial serial object and binds the Wirelink
sink. The adapter must outlive its `wl_ctx_t`. Destruction closes the serial
port, returns any outstanding borrowed RX reservation, forwards an outstanding
TX cancellation, and then unbinds the sink. Bare-metal and Zephyr builds do not
enable or compile this adapter.

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
- `benchmarks/zephyr/rx_backend/` is the build-only ESP32-S3 matrix for the
  fixed BipBuffer implementation with UART IRQ/UART DMA/USB CDC ingress. Its
  physical test procedure and buffer-selection record are in
  `docs/rx-performance.md`.

`tests/zephyr/integration/protocol` is the core in-memory two-peer fixture. It
exercises reliable acknowledgement, a dropped first DATA retry, COBS
byte-at-a-time ingestion, and corrupted-unit rejection without hardware.
`tests/zephyr/integration/uart_dma_adapter` supplies a fake asynchronous UART
device and covers deferred TX completion, BUSY retry, abort/retry, finite RX
restart, and asynchronous stop across the native/QEMU platform matrix.

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
