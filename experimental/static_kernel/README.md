# Static endpoint kernel prototype

This is the isolated K1 work area for the [refactor plan](../../docs/static-endpoint-kernel-refactor-plan-20261030-cn.md).
It is not an installed product mode. Its standalone CMake target does not link
the old Wirelink library.

The first component is the allocation-free C11 packet port. A serialized I/O
producer claims, commits or aborts RX storage; the owner borrows and releases
complete packets. The caller supplies exactly two RX units. Commit validates
the capacity saved at claim time, and a borrow keeps its unit unavailable to
the producer. No codec or business callback runs in the I/O context.

TX has one physical transfer at a time. `CONSUMED` permits reuse of source
bytes; `DRAINED` permits another transfer. A sink returning `BUSY` or `FAILED`
has accepted no bytes and must not subsequently publish a completion. A sink
returning `STARTED` may publish completion before returning. On `BUSY`, the
adapter publishes activity when resources become available; an owner must
wait for that notification rather than spin on the pending message.

Tokens contain a unique nonzero instance ID and a nonwrapping sequence. TX
sequence and completion flags share one lock-free atomic word, so a delayed
old publisher cannot update a newer transfer between its token check and
publication. RX sequences exhaust at `UINT32_MAX`; TX sequences exhaust at
`WL_PACKET_SEQUENCE_MAX` (28 bits). Exhaustion requires a quiescent restart
with a new instance ID, not wrapping IDs. Ordinary ring cursors may wrap.

`stop` rejects new claims and submits and wakes the owner. Existing reads may
abort, queued RX may be drained, and existing TX must finish or be explicitly
aborted by the adapter. `quiescent` checks the port's outstanding ownership;
the adapter must additionally acknowledge that all I/O callbacks have stopped
before reinitializing or destroying the object. Initialization, owner calls
and producer calls must follow the roles documented in the header. The outer
runtime supplies a latched event for its query/wait race; the port's activity
bit can be atomically consumed without losing a concurrent publication.

Build and test:

```sh
cmake -S experimental/static_kernel -B build/static-kernel
cmake --build build/static-kernel --parallel
ctest --test-dir build/static-kernel --output-on-failure
```

The deterministic fixture also runs as the Ztest `type: unit` suite
`tests/zephyr/unit/static_packet_port`. The host concurrency fixture transfers
100,000 RX/TX units between an I/O thread and an owner, including late duplicate
completion publication racing the next submit.

The shared endpoint kernel now owns one four-call table, four request buffers,
four reply buffers and one ordinary-message buffer supplied by generated code.
Requests are snapshotted before admission returns. Terminal callbacks release
logical slots; leased TX bytes remain reserved until physical drain. A callback
may submit/cancel calls, but recursive step/close fails. Generation snapshots
exclude callback-created calls from the current deadline pass.

Reply reservation precedes decoding and handler side effects. BUSY retains the
encoded reply and sleeps until activity. A full reply pool retains the RX
borrow. There is no duplicate history: independently admitted duplicate requests
may execute again. Errors are returned from step and optionally sent to a small
diagnostic callback. The owner supplies time; deadlines use bounded u32 modular
arithmetic. Close schedules one terminal outcome per still-waiting call and
continues draining physical completion before quiescence.

The shared-kernel unit fixture uses a deliberately small test codec. The separate
`generated_usb` fixture uses real WLC-generated Setpoint/Control RPC and
Sample/StreamChunk/Progress routes over the same packet port and kernel. The
business schema is copied byte-for-byte from frozen A; its profile is explicitly
migrated to version 2. WLC rejects removed reliable/replay/mapped/deferred
declarations rather than translating them to the new semantics.

Build the matching generator and enable this fixture explicitly:

```sh
cargo build --manifest-path wlc/Cargo.toml
cmake -S experimental/static_kernel -B build/static-kernel \
  -DWIRELINK_KERNEL_WLC_EXECUTABLE="$PWD/wlc/target/debug/wlc"
cmake --build build/static-kernel --parallel
ctest --test-dir build/static-kernel --output-on-failure
```

The fixture covers four concurrent calls, out-of-order and duplicate replies,
both RPC directions, reply-pool backpressure before side effects, BUSY without
handler reexecution, a response arriving before TX drain, late timeout replies,
business rejection, malformed request/response bodies, response encode failure,
and all seven message types including maximum ControlResponse/StreamChunk data.
These are software fixture handlers; benchmark identity/control handlers and
the product USB adapter remain to be integrated. K1 completion and performance
acceptance have not been established.

The native/NONE connector now writes the four-byte ordinary DATA prefix in
place and validates incoming packets without a general frame encoder, selector,
CRC or COBS dependency. Managed RPC metadata uses the existing v4 session,
tagged operation and rejection encoding. It supplies wire primitives only;
there is no replay, ARQ or server execution layer here.

`kernel_wire` tests include 50 vectors generated from frozen old-A
`managed_rpc_helpers.c.in` (SHA-256
`60f79894bd57c2e0e30b9da4618d087b6dd4caf94eae16d73a9c8d36afcdf8d0`),
truncated/noncanonical/overflow metadata, signed rejection statuses, native
payload bounds and unsupported reliable frames followed by ordinary DATA.
The same tests run in Ztest. External K1 evidence additionally compares one
million metadata cases and all 2,049 legal native payload sizes against frozen
A's helpers and framing tool. The Xtensa `-Os` native connector has no undefined
symbols; this is a code dependency check, not a device performance measurement.

The isolated Zephyr product binding lives in `adapters/zephyr/usb_bulk.c` and
`wirelink/zephyr/kernel_usb_bulk.h`. It preserves the existing descriptor,
net_buf, ZLP and explicit reconfiguration mechanisms. Build it instead of the
legacy USB adapter with `CONFIG_WIRELINK=n`; it has no old core dependency.
This temporary copied binding is to converge with product adapters in K5.

Initialize the packet port first with `wl_kernel_usb_bulk_submit`,
`wl_kernel_usb_bulk_notify`, and `user=adapter`, then bind the adapter with the
selected maximum frame size, strictly less than the RX unit size. The USB
thread only publishes I/O; owner service claims/enqueues RX. Each of the same
two net_buf objects has an additional packet token in user data (32 bytes
total on the current target), preventing an old completion from matching a new
request at the same endpoint. TX borrows the final frame until completion.

Call owner service around kernel step, and combine adapter readiness, kernel
readiness/deadline and the outer latched event when waiting. Stop disables
USBD and port admission. It remains pending until real transfer cancellation
and callback-side adapter/port accesses end; no cancellation is synthesized.
Consume final port completion and release RX borrows before deinit/reinit.
The outer wake callback/context must remain valid through final I/O notification.
Only a fresh, unique instance ID may be used when restarting.

`tests/zephyr/integration/static_usb_bulk` exercises this actual USBD class and
packet port on `native_sim`, with a controlled UDC enqueue boundary. It covers
RX backpressure, inline completion, assigned endpoints/MPS, ZLP, stale tokens,
boundary faults/reconfiguration and asynchronous stop acknowledgement. It
links no legacy core. Hardware benchmark qualification is recorded separately.

K3 public headers have C linkage when consumed from C++. Packet port storage
is defined in C, with an opaque `wl_packet_port_t` in C++; C++ must not assume
that `std::atomic` has the C11 atomic representation. A small C owner may
provide the port pointer to an adapter or generated C++ consumer. Generated
endpoint storage contains no C atomics and can be instantiated in C++. The
`kernel_cpp` fixture links C storage, C core and generated C with a C++20
caller, typed completion, cancellation, close and new-instance reuse.

The K3 host binding `adapters/astrial/packet_usb.cpp` uses the same C packet
port directly. It preserves the Astrial borrowed-read/write API, one in-flight
read and write, and the read-terminator gap. Construct the adapter before port
initialization, configure port submit/notify with the adapter as user, then
start. Only I/O callbacks claim/commit RX and publish token-bound TX completion;
codec and terminal callbacks stay on the owner. Faults are sticky until an
explicit USB reset/reconfiguration and a fresh instance. Stop joins the USB
worker before acknowledging that adapter and port storage can be reclaimed.
The owner drains queued RX/final TX completion and closes its kernel before
reinitialization. Do not call stop/destruction from a USB callback.

Enable `WIRELINK_KERNEL_BUILD_ASTRIAL_USB=ON` for this standalone target and its
controlled Astrial fixture. The fixture links the real adapter and C port,
checks two-slot backpressure and borrowed RX, inline/asynchronous TX, late
completion rejection, latched wakes, boundary/disconnect faults and stop/reinit.
It supplies completion events at the public Astrial boundary, without claiming
to simulate the USB controller. Both GCC and Clang ASan/UBSan pass all 7 tests.

TX admission preserves a single FIFO order across client requests, constructed
replies and ordinary messages. This matters when a caller accepts a stream
Progress marker and then submits a Control STOP RPC. A fixed nine-index queue
refers to the existing TX owners; it adds no second call table or payload pool.
Cancel/timeout removes an unsent index before its storage can be reused; BUSY
keeps the head, and an asynchronous source stays leased until completion.

The independent stream connectors `wirelink_stream_cobs_crc16` and
`wirelink_stream_length16_none` wrap canonical ordinary DATA packets. Each has
its own translation unit and direct entry points, with no runtime envelope
selector or old link engine. COBS uses unchanged CRC16 and COBS primitives;
length16 has no CRC/COBS dependency. Caller-owned wire TX bytes remain alive
through physical drain. RX claims persist across fragments and backpressure;
malformed/oversized units release claims and resynchronize at their framing
boundary. Only a serialized byte producer calls feed; core/typed callbacks run
later on owner step. Integrations where callbacks only publish I/O should stage
bytes and call feed from owner service.

`packet_stream` compares both connectors against the independent public frame
tool for every payload length 0–2048, varying fragments and two-slot pressure,
bad CRC, partial abort and recovery. The public frame tool is only a test oracle,
not a kernel dependency. WLC additionally generates a non-benchmark RPC endpoint
for each connector and runs real request/handler/typed completion loopback.

`wirelink_byte_fifo` is optional SPSC byte staging for stream adapters. An I/O
producer only reserves/commits bytes and reports physical gaps; owner service
passes retained bytes to the selected COBS connector. This keeps framing and
all business work off the ISR. The FIFO has no protocol clock, heap, thread or
RPC state. Its cursors wrap modulo twice the actual capacity, including sizes
that are not powers of two. Producer reservations and owner peek lengths are
validated internally. A gap is an explicit I/O error and invalidates the
partial COBS window until a delimiter. Plain backpressure does not imply loss.
The host fixture transfers one million bytes concurrently, exercises tiny FIFO
fragments across 1000 complete frames, and checks gap recovery and ownership.

K3 temporary Zephyr UART bindings are `kernel_uart_irq`, `kernel_uart_dma` and
`kernel_uart_poll`. They use the same packet port plus independent byte FIFO
and selected COBS/CRC16 framing. Init registers only the actual driver binding;
application assembly wires submit/notify explicitly. Owner service consumes
staged bytes and publishes physical TX completion; no old link/endpoint/pump is
linked. IRQ always requires a real line-idle query. DMA retains its two driver
buffers, ordered staged forwarding, cache hooks, BUSY retry deadline, explicit
buffer-return acknowledgements and callback generation/join barrier.

Polling remains experimental. It requires a platform `tx_idle` provider and
keeps physical TX active after synchronous source consumption. Stop joins the
RX worker and returns WOULD_BLOCK until real idle is confirmed; caller-owned
adapter/stack/port/stream storage stays alive. A driver without physical-idle
support is not silently treated as drained. This is a deliberate migration
from the old polling adapter's weaker completion boundary.

The actual UART adapters pass four controlled-driver cases on native_sim,
Cortex-M3 and RISC-V (12 case/platform executions), including a DMA callback
suspended during shutdown with a full byte FIFO. Shutdown waits for its exit
without requiring unpublished bytes to be parsed. These are software adapter
fixtures; UART hardware throughput and physical timing have not been measured.

K3 packet UDP bindings are `adapters/asio/packet_udp.cpp` and the Zephyr
`kernel_udp` / `kernel_udp_socket` modules. Both receive into caller-owned
packet claims with a guard byte, reject empty/foreign/truncated datagrams,
retain two-slot backpressure and preserve latched waits. A selected in-place
COBS/CRC16 datagram decoder can be explicitly composed; native packets need no
framing wrapper. They do not call old endpoint, link or RPC APIs. A UDP send
completion means whole-datagram socket acceptance and no retained source, not
MAC completion or peer receipt. Zephyr preserves bounded timed retries instead
of spinning on temporary socket pressure. The Asio binding retains an idle RX
claim so a no-data poll does not continually notify itself by claim/abort.

Actual Asio loopback tests (GCC and Clang ASan/UBSan) and actual Zephyr loopback
socket tests cover source/guard checks, held RX, full slots and release, native
and COBS input, notify-before-wait and notify-during-wait, stop and close. They
measure no UDP performance and do not imply Wi-Fi/hardware-network validation.

`wirelink_kernel_astrial_serial` uses the unchanged Astrial borrowed I/O API.
RX callbacks stage only bytes into the C FIFO; owner service runs COBS/CRC16.
Write completion publishes CONSUMED. A required nonblocking platform `drained`
query publishes DRAINED only after the real configured boundary, with a finite
query deadline. Missing drain support is an explicit init error, not a false
completion. Stop closes/joins Astrial before cancellation acknowledgement,
so borrowed storage and callback context remain live until their actual exit.
The real PTY fixture covers partial RX, a tiny staging FIFO, borrowed TX source
snapshot, distinct consumed/drained, latched waiting and stop; it is not a
physical serial-line timing measurement. GCC and Clang ASan/UBSan pass it.

`wirelink_packet_loopback` is a single-owner in-process adapter for the same
packet ports. It retains the submitted source until bounded owner service can
copy it into the peer's RX claim. Full peer slots apply backpressure; stop
confirms cancellation without a driver thread. The generated fixture runs four
calls in each direction concurrently with budget-one core passes, then closes
and drains both endpoints. No old link objects are involved.
