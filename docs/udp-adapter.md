# Asio UDP: lifecycle, storage and limits

[中文](udp-adapter-cn.md). The adapter is optional C++20 host code. Neither Asio,
OS sockets, heap allocation nor waiting enters the C11 core or generated business
code. Build with `WIRELINK_BUILD_ASIO_UDP_ADAPTER=ON` and
`WIRELINK_ASIO_INCLUDE_DIR`; tutorial builds enable the adapter automatically.
The installed target is `Wirelink::asio_udp` (build-tree legacy alias:
`wirelink::asio_udp`). Standalone Asio 1.38.1 is pinned in host CI.

## Default endpoint integration

Initialize the generated endpoint, then call `UdpAdapter::open()` with its
`wl_endpoint_t&` handle. This overload attaches transport service, quiesce and
deadline hooks without replacing generated application hooks. An already attached
endpoint is rejected before changing its transport. Configure a numeric peer
address/port with `set_peer()` before sending; a different peer requires a fresh
link, not mutation of an active session. No DNS, discovery, multiplexer or
multi-client RPC server is added.

Keep endpoint storage alive until the adapter is destroyed. Closing the endpoint
quiesces its adapter. Destroying an attached adapter first closes the endpoint,
before freeing native RX queue memory. After endpoint close, destroying that old
adapter does not close a subsequently initialized endpoint incarnation. Do not
reuse an endpoint while its old adapter is still active.

The advanced `open(wl_ctx_t&, ...)` path installs a sink without pump attachment;
the caller owns scheduling and must stop using/reinitialize the core before
releasing adapter-owned RX storage. All operations are single-owner, including
socket readiness waiting and shutdown; concurrent close is not supported.

## Framing and bounded receive storage

| Mode | UDP payload | Receive path |
| --- | --- | --- |
| Native packet (tutorial) | Exactly one complete Wirelink frame | Receive directly into a core unit-queue claim |
| Explicit legacy COBS stream | One COBS-framed unit | Stage and validate datagram size, then copy into a stream DMA claim |

The initialized link selects framing and integrity; neither mode forces
`WL_INTEGRITY_NONE`. Tutorials retain generated native-packet/CRC32C defaults.
COBS is an explicit compatibility path, not the natural UDP default. A transport
mode change must be coordinated with the peer; nothing auto-detects the format.

`maximum_datagram_size=0` derives the complete-frame bound from the link. An
explicit maximum must cover that bound and cannot exceed 65507 bytes. Native
storage uses `receive_slots` (default 4, range 2–8) times `(maximum + 1)` bytes,
allocated when opening, not per received datagram. One extra byte detects
oversized/truncated datagrams on platforms that silently truncate instead of
returning a socket error. Oversized and empty packets are rejected, never fed as
valid prefixes. Legacy mode keeps one staged datagram until ring space is available.
This deliberately adds a staging copy to the old stream receive path; native RX
avoids it. Other Asio/host operations are not claimed to be allocation-free.

Each `service()` handles at most `service_budget` datagrams (default 8). Full
native queues stop reads until owner dispatch frees capacity; UDP/kernel queues
can still drop packets. Counters expose receives, rejections, pauses and I/O errors.
Unknown sources are dropped diagnostically without failing unrelated calls.

## Waiting and deadlines

Use the generated `endpoint_step()`, obtain `wl_endpoint_get_hint()`, then wait
no longer than the nearest link, RPC or application deadline. `wait_for_activity()`
uses Asio socket readiness with a bounded `io_context` wait; it does not call
Wirelink or business callbacks, and creates no background thread. Readiness only
means another service pass may make progress. Send backpressure also enables
writable readiness. Pending readiness handlers are cancelled and drained before
returning. `wait_calls`, `wait_timeouts` and `activity_notifications` are observable.

Endpoint-attached mode does not add a periodic 1 ms deadline. The legacy bare-link
path retains `poll_interval` as a scheduling hint for existing polling owners.
There is no universal latency/CPU improvement claim; benchmark actual traffic and
host scheduling before tuning budgets. The tutorials are correctness examples,
not firmware or network performance measurements.

## Scope and verification

Tutorials bind only `127.0.0.1` with a fixed peer. Optional first-datagram learning
remains an explicit legacy feature: it is not authentication or validated discovery.
IP/port filtering and CRC also do not authenticate a sender. UDP maximum sizes
are local bounds, not path-MTU discovery; avoid IP fragmentation and design rate,
congestion and security policy before exposing traffic beyond a controlled setup.

`tests/tutorials/udp_processes.py` starts real C publisher/subscriber and RPC
client/server processes. Python forwards/drops/duplicates/reorders packets; it
never manufactures acknowledgements or executes RPC. Tests cover success,
rejection, request/response retransmission, duplicates, reordered retries and
complete request loss. Adapter tests cover both framing modes with NONE/CRC32C,
queue pressure, readiness timeout/wakeup, wrong sources, oversize rejection and
close/reinitialization. Release tests use checks that remain active with `NDEBUG`.
The old combined tutorials remain under `tests/tutorials/loopback/`.

The clock evolution uses generated ABI 21 without changing frozen core frame/codec bytes.
The existing limitation around old RPC responses after client reconstruction and
wire-ID reuse still applies; see [RPC correlation](rpc-runtime.md).

### One clock for link and RPC

The endpoint now accepts a clock at initialization. Reliable submission samples
it for both link and RPC timing; a step snapshots it before adapter completions
and reuses it for inline replies. No preliminary step is needed before sending.
An outside hint query samples time without polling. See [clock contract and
migration](endpoint-clock.md). UDP readiness only wakes the owner; it does not
replace the clock or deadline policy.
