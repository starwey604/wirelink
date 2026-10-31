# Static endpoint kernel API

This is the default Wirelink 0.13 C11 execution implementation. Public headers
and the [migration guide](migration-0.13.md) ([中文](migration-0.13-cn.md)) define
the product contract. Research registrations/results record separate builds;
K4 measurements do not automatically describe the final migrated product.

## Shared execution and generated code

`src/kernel.c` owns four outbound calls, four reply snapshots, one ordinary
snapshot and a nine-index admission FIFO. Generated C supplies typed codecs,
static routing and message-specific storage bounds. It does not generate another
call table, replay cache or service scheduler. Host Executor, C++ Session and SDK
bridges invoke this same core. `Wirelink::wirelink` links the kernel/types only.

`wl_kernel_call_start` receives request/response IDs, encoder, caller `now`, a
timeout in `1..INT32_MAX` ticks, completion/context and an output handle.
Generated `*_kernel_<rpc>_start` supplies the typed mapping. Success snapshots
the request before return; failure produces no future completion. A fifth
concurrent call returns `WL_ERR_WOULD_BLOCK`. Terminal handles become invalid;
operation/generation counters exhaust explicitly instead of wrapping.

`wl_kernel_step(core, now, budget)` advances bounded owner work. Deadline
processing drains all eligible terminals before RX; if the event budget is
exhausted, RX stays queued. A generation snapshot excludes callback-created calls
from the current deadline pass. Callbacks may submit/cancel, but recursive
step/close is rejected. The owner supplies a monotonic modular `uint32_t` tick;
there are no clock reads, threads, OS calls or heap allocations in core code.

Every budget from 1 through `WL_KERNEL_BUDGET` is supported. RX/TX preference
persists across owner passes: after a TX submission, queued runnable RX receives
a turn before another submission, even if completion consumed a whole pass.
Physical completion and eligible terminals still take precedence. Empty or
reply-pressure-blocked RX does not prevent TX progress; receiving continuously
does not remove TX's turn. This bounds I/O starvation, not elapsed response time
under an arbitrary external owner schedule. Rebuild consumers after private
kernel storage layout changes.

Each accepted call receives one terminal callback: success, business rejection,
local error, timeout, cancellation or close. Response views borrow storage only
until callback return. Copy via public value conversion to retain nested data.
Local cancellation/timeout cannot reverse remote execution. Retrying creates a
new operation and can repeat side effects; application IDs/idempotency are
required when that matters.

Before decoding a request or executing its handler, the router reserves reply
storage. A full reply pool retains RX and applies pressure. Synchronous handlers
fill an owned response and return a signed business status. BUSY retains the
encoded response and waits for activity without invoking the handler again.
Independently admitted duplicate requests may execute again. No deferred reply
token, TTL, replay cache or peer-state hook remains.

## Packet port ownership

`src/packet_port.c` owns the allocation-free packet exchange boundary. C supplies
exactly two RX units and all atomic object storage. A serialized I/O producer
claims/commits/aborts; the single owner borrows/releases complete packets.
Claim-time capacity is validated at commit. A borrowed unit cannot be reused.
No codecs, handlers or terminal callbacks execute on the I/O thread/ISR.

One physical TX may be outstanding. A sink returns:

| Result | Ownership / progress |
| --- | --- |
| `WL_PACKET_SENT` | Source actually consumed and transfer physically drained |
| `WL_PACKET_STARTED` | Source leased; token-bound completion may even occur before return |
| `WL_PACKET_BUSY` | Nothing accepted; publish activity when a resource becomes available |
| `WL_PACKET_FAILED` | Nothing accepted; no later completion allowed |

`WL_PACKET_CONSUMED` permits source reuse; `WL_PACKET_DRAINED` ends physical
transfer ownership. Logical terminal and physical lifetime are independent.
The kernel keeps leased snapshot bytes alive even after timeout/cancel. A
callback counter or queue-empty observation cannot manufacture physical drain.
BUSY consumes the runnable opportunity and waits for real I/O activity.

Tokens contain a fresh nonzero instance and a nonwrapping sequence. TX sequence
and completion flags share one lock-free atomic word, so an old publisher cannot
update a newer transfer between token validation and publication. RX sequences
exhaust at `UINT32_MAX`; TX at `WL_PACKET_SEQUENCE_MAX` (28 bits). Initialization
can reject a platform without the required lock-free primitive. Ordinary ring
cursors may wrap; identity counters may not.

Public `wl_packet_submit_t` and `wl_kernel_outcome_t` use fixed `int32_t`
storage/returns, including when a consumer uses `-fshort-enums`.
C++ sees opaque packet/FIFO objects. Define their storage in C and pass pointers
to C++ adapters; `std::atomic` is not a C11 atomic ABI substitute. Generated
kernel storage itself contains no C atomic members.

## Connectors and adapters

The core exchanges canonical native/NONE ordinary DATA packets. The
[native wire](packet-protocol.md) retains a four-byte prefix and the managed v4
RPC session/operation/status metadata. Reliable DATA/ACK are explicitly
unsupported; independent frame tools still cover frozen historical formats.
Public codec schema domains and message IDs are separate from execution policy.

Two explicit stream connectors wrap canonical packets: COBS/CRC16 and
length16/NONE. TX worst-case wire capacity is checked independently from RX
capacity. A C-owned SPSC byte FIFO may stage callback input, including
non-power-of-two capacities. Pressure pauses/resumes without declaring byte
loss. A real discontinuity aborts/resynchronizes the current stream window.

[Adapter contracts](adapters.md) describe native USB, serial/UART, UDP and
loopback. USB preserves strict read headroom, MPS/ZLP, stale token validation,
latched boundary faults and real reset/reconfiguration. DMA waits for actual
buffer returns. IRQ/poll/host serial require a genuine physical completion
boundary. Unsupported CDC TX-complete capability returns NOT_SUPPORTED.

## Waiting and shutdown

`wl_kernel_ready` reports runnability and the earliest client deadline. The
outer runtime merges adapter readiness and application deadlines, uses a latched
wake across query/wait and rebases finite waits after owner work. The port
activity bit alone does not close the operating-system wait race.

`wl_kernel_close` rejects admission and schedules terminal outcomes. Driver
stop must reject new I/O and wait for actual cancellation, buffer return and
callback/worker exit. Continue owner completion processing until
`wl_kernel_quiescent` and port silence. Only then free/reinitialize storage,
with a new instance identity. Quiescent core state does not prove that an
external callback has exited; the adapter must establish that separately.

Executor bounds its external command queue to 32 and processes at most eight
commands before kernel work. Work/rejection runs on the owner. Owner callbacks
may request_stop but must not join or destroy their own owner/Session. External
Session close waits for submitters and joins I/O/owner before freeing C storage.
There is no mirrored RPC timer/call table in the wrapper.

## Build-backed examples and tests

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
```

The default bare-metal loopback uses checked-in generated C. With a matching
local WLC 0.13, enable `WIRELINK_BUILD_WLC_EXAMPLES=ON` and set
`WIRELINK_WLC_EXECUTABLE` to build the current tutorials. A v2 profile uses one
`wirelink_wlc_generate` target; no-profile generation emits pure public codec.

Core fixtures cover four calls/replies, request snapshots, stale/out-of-order
responses, reply reservation before effects, BUSY, rejection/malformed bodies,
callback reentry, timeout/cancel/close and leased TX. Concurrency fixtures keep
atomics in C and create/join threads in C++. Package/SDK tests use installed
headers and independent C peers. Ztest adapter fixtures use controlled driver
boundaries; actual hardware capture/build identity is recorded separately.
