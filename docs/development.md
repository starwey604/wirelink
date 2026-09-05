# Development policy

## WLC compiler baseline

The Rust compiler is developed in the adjacent `wlc/` worktree. It owns the
versioned `.wl` grammar, AST, source-located validation, semantic model,
compatibility checks, and deterministic C generator. The supported baseline
constructs are `message`, `enum`, `optional`, `required`, `repeated`,
fixed-count `packed`/`required packed`, and `default`; see
`docs/schema-v1.md` for the normative v1 contract. Exact-width 8/16/32/64-bit
integers, native `float32`/`float64`, and packed fixed-width numeric arrays
provide compact control payloads without heap allocation or per-element tags.
`string<MAX>` and `bytes<MAX>` retain borrowed zero-copy C views while making
the accepted byte domain and generated static encoded-size ceiling explicit.

Run its focused checks from the repository root with:

```sh
cargo test --manifest-path wlc/Cargo.toml
```

Consumer builds use the released host compiler instead of building this Rust
worktree. `wirelink_wlc_generate_codec()` resolves a per-call executable, a
project-wide executable, or a compatible `wlc` on the host `PATH` before
downloading the pinned release. The fallback selects from
`CMAKE_HOST_SYSTEM_NAME`/`CMAKE_HOST_SYSTEM_PROCESSOR`, verifies a source-pinned
SHA256, and caches the extracted executable below
`WIRELINK_WLC_CACHE_DIR`. It never follows a branch or a `latest` release and
never selects from the target sysroot during a cross-build. Set
`WIRELINK_WLC_AUTO_DOWNLOAD=OFF` and `WIRELINK_WLC_EXECUTABLE` for an offline
tool cache.

Wirelink pins both the WLC release version and its codegen ABI. Every generated
manifest is checked at build time before generated C compilation; updating WLC
therefore requires updating the version, per-host archive hashes, expected ABI,
fixtures, and package-consumer tests together.

WLC emits a codec pair (`<module>.h/.c`) and a binding pair
(`<module>_bindings.h/.c`). The codec exposes allocation-free clear,
encoded-size, encode, and decode functions and depends only on
`wirelink/codec.h`. The separately linkable binding translation unit adds
typed routing, scratch sends, and native direct sends against the public core
API. Both compile as ISO C11; no Rust runtime is present on the target.

`wirelink_wlc_generate_runtime()` consumes an existing codec target and emits
only `<runtime-name>_runtime.h/.c`. Several profile targets can therefore link
one codec target. A distinct `RUNTIME_NAME` gives each runtime its own C
namespace; its generated code continues to call the shared codec/binding
symbols. Codec and runtime manifests are verified independently.

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

The baseline treats a field's number, name, resolved type, cardinality, and any
borrowed-field byte bound as wire identity. For a packed array, cardinality
includes the exact element count. Reordering source declarations or fields is
semantically inert. `wlc
validate --previous` and `wlc compile --previous` expose compatibility checking
at the CLI boundary before generation.

## Persistent testing decision

Wirelink uses Zephyr's Twister and Ztest for the normative behavioral suite.
This includes tests of the platform-independent C core, which run as Ztest
`unit_testing` scenarios instead of duplicating every case in a host framework.
CTest is used for host-only integration concerns: Astrial pseudo-terminals,
installed-package consumption, executable examples, and bounded fuzz smoke
runs.

The frozen v1 C surface, lifecycle decisions, and the only two approved
pre-1.0 additive API candidates are recorded in `docs/api-v1-audit.md`.

The top-level target is compiled as strict ISO C11. GCC and Clang builds enable
`-Wall`, `-Wextra`, and `-Wpedantic`; warnings in Wirelink sources are treated
as release blockers even when a consumer supplies different project defaults.

## Zephyr architecture

`src/` and `include/wirelink/` are the transport- and RTOS-independent C
protocol core. They must not depend on Zephyr kernel APIs, devicetree, or
drivers. The repository is a Zephyr module through `zephyr/module.yml`:

- `zephyr/Kconfig` controls whether the core is included (`CONFIG_WIRELINK`).
- `zephyr/CMakeLists.txt` builds the existing C sources as a Zephyr library.
- UART, USB, SPI, and UDP adapters belong outside the core and depend on its
  transport interface. Only adapters that use Zephyr APIs belong in
  Zephyr-specific sources.

This keeps the same protocol implementation usable in bare-metal, other RTOS,
and desktop applications.

The RX ring is a fixed, internal atomic SPSC BipBuffer implementation. Its
standalone predecessor and public `wl_bb_*` API were removed before v1; the RX
backend does not enter the public API and Wirelink intentionally exposes no
build-time backend selection. The producer release-publishes its write cursor
and the consumer release-publishes reclaimed space; initialization requires
the C11 atomics to be lock-free on the target. The selection rationale and
ESP32-S3 measurements are retained in `docs/rx-performance.md`.

`wl_ctx_t` is fixed-size, correctly aligned opaque storage. Applications can
allocate it statically without knowing protocol internals. Its size and
alignment are part of the v1 ABI; internal state is guarded by a build-time
assertion so future changes cannot silently exceed the reserved storage.

## Core storage contract

The v1 core has one application TX slot, one control/ACK TX slot, and one RX
event slot. `wl_init()` receives a `wl_storage_t`; its buffers remain owned by
the caller and must outlive the context. The configuration and storage
descriptor values are copied during initialization, so those two input
structures may be temporary. Use `wl_config_requirements()` before allocation
to obtain the exact worst-case buffer sizes for the configured profile. A
profile's `max_transmission_unit`, when nonzero, must accommodate the complete
envelope including COBS delimiters or a length prefix.

Application bytes are copied before a send is accepted and are encoded into
the caller-supplied TX unit buffer. The pointer given to a `WL_SINK_STARTED`
callback remains valid until its matching `wl_tx_complete()`. ACK/control
units use their own buffer and are submitted before a queued application unit.
Unreliable local completions are represented by a bounded counter rather than
the single event slot. `wl_poll()` materializes one `WL_EVT_TX_SUCCESS` at a
time, so synchronous sends, deferred completions, and BUSY retries cannot lose
a completion while an RX event occupies that slot. A saturated counter rejects
the next unreliable send before serialization or sink invocation.

For `NATIVE_PACKET`, `wl_tx_payload_claim()` exposes the payload region inside
the final TX unit. After the caller fills it, `wl_tx_payload_commit()` writes
the header and integrity trailer around those bytes without staging the
payload. The claim is single-owner, and reliable retransmission keeps the same
unit borrowed until the transaction reaches a terminal state.

COBS stream bytes enter a single-producer/single-consumer RX ring. The producer
side (`wl_feed_bytes()` or `wl_rx_reserve()`/`wl_rx_commit()`) only publishes
bytes; COBS decoding, frame validation, ACK scheduling, and event creation run
from the consumer-side `wl_poll()`. A contiguous frame is decoded in place and
its event payload borrows the ring. A frame crossing the ring boundary is
copied once into the caller-supplied fallback buffer. Native packet and bus
units also use that fallback buffer to make their synchronous input lifetime
explicit.

Packet adapters with a true unit boundary can instead configure
`wl_rx_unit_queue_init()`. Its fixed slots form an atomic SPSC handoff: an ISR,
DMA completion, or socket producer only calls `wl_rx_unit_claim()` and
`wl_rx_unit_commit()`, while `wl_poll()` validates and dispatches the unit.
Event payloads borrow the slot in place and keep it outside the producer's free
set until `wl_event_release()`. A full queue is explicit backpressure;
reliable traffic recovers through the existing retry path.

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

## Consumer scheduling contract

`wl_poll_get_hint()` is the allocation-free bridge from the protocol state
machine to a platform scheduler. Its `wl_poll_hint_t` contains two fixed-width
fields: `work_pending` and `next_deadline_ms`. The latter is relative to the
query's `now_ms`; `WL_POLL_NO_DEADLINE_MS` means the core has no timed work.
The query neither updates `ctx->now_ms`, consumes RX storage, calls the sink,
nor changes retry state.

A consumer should drain available events, release each borrowed RX event, ask
for a hint, and sleep only when `work_pending` is zero. The wait deadline is
the minimum of `next_deadline_ms` and any application/adapter deadline:

```c
for (;;) {
  wl_event_t event;
  wl_poll_hint_t hint;
  wl_time_ms_t now_ms = platform_now_ms();

  while (wl_poll(&link, now_ms, &event) == WL_OK) {
    handle_event(&event);
    wl_event_release(&link, &event);
    now_ms = platform_now_ms();
  }
  (void)wl_poll_get_hint(&link, now_ms, &hint);
  if (hint.work_pending == 0U) {
    adapter_wait_for_activity(hint.next_deadline_ms);
  }
}
```

Every RX publication, deferred TX completion, and asynchronous sink-writable
transition must notify the platform wait primitive. After an external wake,
the loop calls `wl_poll()` before consulting the next hint; that call services
one pending control/DATA submission. If the sink returns `WL_SINK_BUSY` again,
the new hint remains non-immediate and the loop sleeps for the next writable
notification instead of spinning. An adapter without a writability callback
must provide its own bounded periodic wake policy.

Hosted adapters expose the same owner-facing lifecycle: `service()` publishes
deferred completion/rearm state, `quiesce()` stops producers and detaches the
sink, and `deadline_hint(now_ms)` returns a relative scheduling bound.
Astrial serial and USB Bulk are event-driven: their completion callbacks wake
the owner and their deadline is `WL_POLL_NO_DEADLINE_MS`. The synchronous
non-blocking Asio UDP adapter cannot expose socket readiness through its
cross-platform API, so its configuration supplies a finite `poll_interval`
(1 ms by default). The owner merges this adapter hint with core and
application hints through `wl_pump_get_hint()`.

Hosted adapters also expose `get_common_stats(wl_adapter_stats_t&)`. The
transport-independent view is the acceptance surface for telemetry and HIL:
RX/TX units and bytes, backpressure, TX completions, owner notifications,
service passes, errors, and current started/paused/active flags. Adapter-
specific statistics remain available for controller or backend diagnostics,
but product tests should use the common counters for pass/fail rules.

For COBS ingress, the query scans only for a complete delimiter-terminated
unit; a partial unit waits for the producer's next notification. RX overflow
is immediate recovery work even when the ring contains no complete unit.
Native packet slots use the unit queue's acquire cursor. Buffered RX behind a
leased event is intentionally hidden until release, while pending TX events
and ACK deadlines remain visible. The query belongs to the same single
consumer as polling, release, TX completion, and adapter service; the SPSC
producer must not call it from an ISR or DMA callback.

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

The Astrial PTY fixture also compiles the current generated control schema and
runs a typed six-joint command through WLC encode, Wirelink COBS framing, the
OS serial path, borrowed ring ingestion, and WLC decode. Decoded `string` and
`bytes` pointers are asserted to lie inside the leased Wirelink event payload.
A second frame is buffered but not delivered until `wl_event_release()`, which
ties the generated codec's borrowed fields to the core event lifetime.

`adapters/astrial_usb/` maps Astrial's optional libusb Bulk backend to the same
consumer-side service contract. The host controller writes directly into a
`wl_rx_dma_claim()` span; the libusb event thread publishes and finishes that
claim, while protocol polling remains in the application thread. TX borrows
the stable Wirelink unit and forwards completion through an atomic mailbox.

The direct USB mapping uses exactly one outstanding IN transfer. This is a
consequence of variable-length Bulk transfers rather than a libusb limitation:
a short first transfer must return its claim's unused tail, which the ring can
only do before a successor claim is allocated. Two queued direct claims would
create a logical hole. A four-transfer adapter-owned staging prototype was
measured and rejected: its copy and 2,304 bytes of extra buffers produced no
meaningful RTT, goodput, or CPU improvement. Because Astrial's USB callbacks
already form one producer and Wirelink's ring is the SPSC handoff, no external
lock-free queue or selectable USB RX backend is retained.

`adapters/zephyr/usb_bulk/` provides the matching Zephyr USB-next custom class
with vendor subclass `0x57`, protocol `0x4c`, Bulk OUT `0x01`, and Bulk IN
`0x81`. Initialize its singleton adapter before `usbd_init()` or the sample USB
setup helper. One externally backed `net_buf` wraps a direct ring claim for
OUT; a second wrapper permits simultaneous borrowed IN. Endpoint completion
publishes only direct-claim and atomic-mailbox state. The consumer loop calls
`wl_poll()` and then `wl_zephyr_usb_bulk_service()` to re-arm OUT and advance TX
completion. Exact-multiple IN units request a ZLP so host reads terminate
without a timeout.

Both USB Bulk adapters also accept `NATIVE_PACKET`. In that mode their receive
buffers are Wirelink unit-queue slots rather than byte-ring claims, so the
controller writes a complete unit directly into storage later borrowed by the
event. Astrial owns four slots by default; the allocation-free Zephyr adapter
requires the application to supply the slot array in its configuration.

The v1 USB vendor-bulk and UDP product profiles use
`COBS_STREAM + WL_INTEGRITY_NONE`. Each USB application transfer or UDP
datagram carries complete delimiter-terminated units. This keeps the same v1
framing on both transports while removing redundant Wirelink CRC work;
UDP/IPv4 deployments must keep UDP checksums enabled.

`adapters/zephyr/uart_irq/` is the generic interrupt-driven fallback used by
the CDC ACM comparison. Its callback drains the UART FIFO straight into a
producer reservation and fills TX from Wirelink's borrowed unit; it performs
no parsing, event delivery, ACK work, or retry scheduling. Ring backpressure
disables RX IRQ delivery until the consumer calls `service()`. A hardware UART
keeps the borrowed TX unit until `uart_irq_tx_complete()` reports physical
idle. Zephyr CDC ACM does not implement that query, but its `fifo_fill()` has
already copied the bytes into the class-owned FIFO, so `-ENOSYS` is the correct
memory-ownership completion boundary for that device.

Reliable TX handles contain a slot-generation value. A terminal reliable
transaction remains queryable until `wl_tx_take()` returns its result, after
which the old handle is invalid. The ACK timer starts only once the local sink
reports completion; retries are counted separately from the initial send.
Reliable RX deduplicates the most recent `(session_id, sequence)` and re-ACKs
duplicates without emitting another event.

## Test layers

- `tests/zephyr/unit/`: Twister `type: unit` tests using the host-native
  `unit_testing` board. Use for pure C modules such as the RX ring, CRC, COBS,
  frame codecs, and ARQ state transitions.
- `tests/zephyr/integration/`: Ztest applications run on `native_sim`, QEMU,
  or hardware. Use for protocol-stack and adapter integration.
- Multi-device transport tests use Twister's pytest harness and a hardware map
  when real DUTs are required.
- `benchmarks/zephyr/rx_backend/` is the build-only ESP32-S3 matrix for the
  fixed BipBuffer implementation with UART IRQ/UART DMA/USB CDC ingress. Its
  physical test procedure and buffer-selection record are in
  `docs/rx-performance.md`.
- `tests/zephyr/unit/generated_codec/` compiles the previous and current WLC
  artifacts independently. It covers unknown-field skipping, absent added
  fields and defaults, borrowed length-delimited values, repeated capacity,
  duplicate fields, UTF-8, wire-type, and malformed-input failures.
- `tests/zephyr/unit/conformance/` reproduces the exact v1 transmission units
  and freezes decoder rejection classes.
- `tests/zephyr/unit/bulk_sender/` and `bulk_receiver/` freeze the sequential
  action/Status lifecycle, retry deadlines, idempotence, sink ownership,
  Abort tombstones, and reset rules.
- `tests/zephyr/integration/bulk_transfer/` routes the generated WLC messages
  through two real Wirelink contexts, including a 1 MiB object, resume, a lost
  Status retry, borrowed Chunk storage, and failed final integrity.
- `fuzz/` contains Clang libFuzzer harnesses for COBS, frames, chunked protocol
  streams, generated codecs, and both bulk state machines. CTest limits each
  smoke target to 5000 runs; longer corpus-guided jobs may invoke the
  executables directly.
- `tests/package/consumer/` is a separate CMake project that validates the
  installed `Wirelink::wirelink` target.
- `examples/` and `samples/zephyr/uart_dma/` are compile-checked integration
  references for bare-metal, desktop, and Zephyr users.

`tests/zephyr/integration/protocol` is the core in-memory two-peer fixture. It
exercises reliable acknowledgement, a dropped first DATA retry, COBS
byte-at-a-time ingestion, and corrupted-unit rejection without hardware.
`tests/zephyr/integration/uart_dma_adapter` supplies a fake asynchronous UART
device and covers deferred TX completion, BUSY retry, abort/retry, finite RX
restart, and asynchronous stop across the native/QEMU platform matrix.

## Continuous integration

GitHub Actions builds the C11 core, Astrial serial adapter, optional
Astrial/libusb Bulk adapter, and host benchmark on Linux, macOS, and Windows.
Linux additionally runs the Astrial PTY path under ASan and UBSan plus bounded
Clang fuzz targets. Every host platform installs the core and builds an
external package consumer. A separate Zephyr job uses the pinned west manifest
and executes all unit and integration scenarios on
`unit_testing`, `native_sim`,
`qemu_cortex_m3`, `qemu_riscv32`, and `qemu_x86_64`.

## Packaging and examples

`WIRELINK_INSTALL` defaults on only when Wirelink is the top-level project.
It installs the core and portable loopback archives, public headers, a
relocatable CMake package, a pkg-config file for the core, and the license.
Private RX state and hardware adapters are not installed. Until the project
reaches 1.0, generated package compatibility is restricted to the same `0.x`
minor release.

`WIRELINK_BUILD_EXAMPLES` also defaults on only for top-level builds. The
bare-metal and typed quickstart examples use `Wirelink::loopback`; the former
is registered with CTest. The typed Astrial example is
added only when `WIRELINK_BUILD_ASTRIAL_ADAPTER=ON`. The Zephyr UART/DMA sample
is a standalone Zephyr application and therefore is built with `west`, not by
top-level CMake.

## Application-layer extensions

Typed routers, the SPSC `LATEST` mailbox, the ordered SPSC `FIFO`, the RPC
client/server runtime, and the sequential bulk sender/receiver are built above
the core rather than into adapter callbacks. Their callback lifetime,
threading, multiplexing, and storage rules are frozen in
[`application-layer.md`](application-layer.md). New implementations must keep
the core's single-consumer state machine and borrowed-event ownership intact.
`wirelink/bulk.h` retains only constant-size transfer state: source bytes are
read while an action is encoded, and received Chunk bytes are consumed by a
synchronous caller sink before the RX event is released. Cross-thread session
executors remain future work; bulk transfer does not add fields to the v1 link
header.

## Integration platform matrix

Use `native_sim` as the default runnable integration target: it is fast and is
the appropriate host environment for virtual transport pairs and UDP tests.
`adapters/asio_udp/` is the cross-platform v1 UDP path. It exposes no POSIX
file descriptors: the caller binds an address, selects a validated peer, and
calls `service()` from the Wirelink consumer loop. The socket is non-blocking,
one complete `COBS_STREAM + NONE` unit is sent per datagram, and RX writes
directly into a ring claim.
For build-and-run coverage on emulated CPU architectures, use this initial
matrix:

| Platform | Purpose |
| --- | --- |
| `native_sim` | Default host integration test and virtual transport peer. |
| `qemu_cortex_m3` | 32-bit Cortex-M embedded ABI and constrained-RAM checks. |
| `qemu_riscv32` | 32-bit RISC-V toolchain and architecture coverage. |
| `qemu_x86_64` | 64-bit QEMU coverage independent of the native host binary. |

Only integration tests should list these platforms in `testcase.yaml`; pure-core
tests remain `type: unit` and run exclusively on `unit_testing`. Hardware
scenarios are added separately with explicit driver and fixture requirements.

From an existing Zephyr workspace, run the current RX-ring unit test with:

```sh
cd /path/to/zephyr
../.venv/bin/west twister -T /path/to/wirelink/tests/zephyr/unit/rx_ring \
  -p unit_testing
```

For a standalone Wirelink workspace, initialize this repository as a west
manifest repository and run `west update`; `west.yml` pins the Zephyr release
and imports its standard dependency manifest.
