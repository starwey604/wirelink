# Kernel and packet-port API

[中文](kernel-api-cn.md) · [Execution model](kernel.md) · [Adapter lifecycle](adapters.md)

Use generated typed entry points for application messages and RPC. Use the
packet port when implementing a transport. This reference covers every operation
in [`kernel.h`](../include/wirelink/kernel.h) and
[`packet_port.h`](../include/wirelink/packet_port.h), including route helpers.
[Wire primitives](../include/wirelink/kernel_wire.h) and
[connectors](../include/wirelink/packet_stream.h) have separate interfaces.

Find an operation:

- Kernel: [initialize](#kernel-initialization), [call](#admit-an-rpc),
  [cancel](#cancel-a-call), [send](#admit-an-ordinary-message),
  [step](#advance-owner-work), [ready](#query-readiness-and-deadline),
  [close/quiescent](#close-and-query-quiescence),
  [route helpers](#generated-route-operations).
- Port: [initialize](#initialize-a-packet-port),
  [produce RX](#produce-rx-claim-commit-or-abort),
  [consume RX](#consume-rx-and-query-space), [submit TX](#submit-physical-tx),
  [completion](#publish-and-take-tx-completion),
  [activity](#activity-and-wake-notification), [stop](#stop-a-port-and-establish-silence).

## Execution contexts and common preconditions

- **Owner:** serializes all kernel operations, RX borrow/release, TX
  submit/take and owner-side queries. Application handlers, completion callbacks
  and `on_error` run here, during `wl_kernel_step`.
- **RX producer:** serializes claim/commit/abort. A claim is private writable
  storage until committed; the owner only sees committed packets.
- **I/O publisher:** publishes token-bound TX completion and resource activity.
  Publication can occur during sink submission, before it returns.
- **Initialization:** runs before I/O starts. Reinitialization requires stopped
  producers, no outstanding claim/borrow/physical lease, and fresh identity.

Unless an error case below explicitly accepts a null pointer, supply valid
initialized objects and valid output pointers. Query functions generally do not
check arguments. Published pointers, callback contexts and backing storage must
remain valid for the lifetime of the operation. Copying a live kernel, port or
atomic object does not create an independent endpoint. Struct fields are private
even where C exposes storage layout; do not alter them to drive state changes.
There is no lock around owner operations: `WL_ERR_REENTRANT` is not a concurrent
access detector or permission to call from multiple threads.

## Types and ABI

`wl_err_t`, `wl_kernel_outcome_t` and `wl_packet_submit_t` are `int32_t` types;
their named constants are defined in enums. Their result storage/return ABI
does not shrink under `-fshort-enums`. This does not make private layouts or all
compiler options ABI-independent. Rebuild consumers with compatible headers,
generated code and toolchain settings when storage layout changes.

`wl_kernel_result_t` contains `outcome` and signed `status`.
`wl_kernel_call_handle_t` contains `uint64_t instance`, `uint32_t generation`,
`uint32_t operation` and `uint8_t slot`; treat it as one opaque identity.
`wl_packet_token_t` contains `uint64_t instance` and `uint32_t sequence`.

C owns C11 atomic packet-port and byte-FIFO storage. Their C++ declarations
are opaque: allocate/define storage in C and pass pointers across the boundary.
`std::atomic` is not a replacement for the C atomic representation. Generated
endpoint storage itself has no C atomic members. See the C/C++ pair
[`cpp_owner.c`](../tests/core/cpp_owner.c) and
[`cpp_consumer.cpp`](../tests/core/cpp_consumer.cpp).

## Capacities

| Resource | Limit / definition |
| --- | --- |
| Outbound calls | `WL_KERNEL_CALLS = 4` |
| Request snapshots | Four, independent of logical call-slot lifetime |
| Reply snapshots | `WL_KERNEL_REPLIES = 4` |
| Ordinary-message snapshots | One |
| Admission FIFO | `WL_KERNEL_TX_COUNT = 9` snapshot indices |
| RX units | `WL_PACKET_RX_SLOTS = 2` |
| Physical TX | One active transfer per port |
| Step budget | `1..WL_KERNEL_BUDGET`, currently `1..16` events |
| Call timeout | `1..INT32_MAX` caller ticks |
| Call operation / per-slot generation | Nonwrapping `uint32_t` identities |
| RX claim sequence | Exhausts at `UINT32_MAX`, including aborted claims |
| TX attempt sequence | Exhausts at `WL_PACKET_SEQUENCE_MAX = 0x0fffffff` |

TX attempts that reach the sink consume an identity even if it returns BUSY or
FAILED. Exhaustion returns `WL_ERR_ID_EXHAUSTED`; close, stop/join, reach
quiescence and reconstruct with a fresh nonzero instance. Ring cursors may
wrap; identity counters must not. Generate an instance that cannot collide with
still-observable work, including across restarts; `wl_packet_init` only checks
that it is nonzero, not that it is globally fresh.

## Kernel initialization

```c
wl_err_t wl_kernel_init(wl_kernel_t *kernel,
                        const wl_kernel_config_t *config);
```

`config` binds an initialized `port`, a nonnull `route`, optional `on_error`,
its `user` context and nine writable `tx_storage` spans. Indices `0..3` are
request frames (at least 17 bytes each), `4..7` reply frames (at least 21 bytes
each), and `8` the ordinary frame (at least 4 bytes). These are initialization
minima, not sufficient capacity for every selected business message. Each span
must have independent, live storage; initialization copies descriptors, not
their backing bytes. WLC supplies codec-specific sizes automatically.

Returns `WL_OK`, or `WL_ERR_INVALID_ARG` for null kernel/config/port/route, zero
port instance, null span data or insufficient minimum span capacity. It does
not initialize the port, start I/O or validate that the port is running. It
clears kernel state and retains pointers; never call it on active work.

**Typed use:** after initializing `port`, call
`demo_kernel_init(&endpoint, &port, &handlers, on_error, user)` as in the
[checked-in example](../examples/bare_metal_loopback.c). Generated init copies
the handler table and binds its static route/storage to this operation.

## Admit an RPC

```c
wl_err_t wl_kernel_call_start(
    wl_kernel_t *kernel, uint16_t request_id, uint16_t response_id,
    const void *value, wl_kernel_encode_fn encode,
    uint32_t now, uint32_t timeout, wl_kernel_complete_fn complete,
    void (*callback)(void), void *user, wl_kernel_call_handle_t *out);
```

**Parameters and ownership.** IDs must be nonzero and different. `value` is
encoder input, borrowed only during this operation; low-level null-value
validity belongs to the encoder. `encode`, `complete`, `callback` and `out`
must be nonnull. `user` is saved for completion and must remain live until it
returns. `now` and `timeout` use the same owner tick as `step`; timeout is
`1..INT32_MAX`. The deadline is modular `now + timeout`, including queue time.

**Success.** `WL_OK` means the complete request snapshot is encoded, a waiting
call and FIFO entry exist, and `*out` identifies the call. It does not mean the
sink has accepted TX. The input can immediately be changed or freed. No
terminal callback runs inline. A request snapshot is separate from the logical
slot and may outlive its terminal if leased to I/O.

**Failure.** Every failure below creates no call, no new valid handle and no
future terminal callback; the caller decides whether/when to retry:

| Result | Condition |
| --- | --- |
| `WL_ERR_INVALID_ARG` | Null required pointer, invalid IDs or timeout |
| `WL_ERR_INVALID_STATE` | Kernel is closed |
| `WL_ERR_WOULD_BLOCK` | No free call slot or no free request snapshot |
| `WL_ERR_ID_EXHAUSTED` | Operation counter exhausted, or available call slots have exhausted generations |
| Encoder error | Encoder returned an error; generated wrappers map codec failure to `WL_ERR_CORRUPT_PAYLOAD` |
| `WL_ERR_BUF_TOO_SMALL` | Encoder length exceeds capacity or metadata/frame does not fit |
| `WL_ERR_PAYLOAD_TOO_LONG` | Encoded native payload exceeds the protocol limit |

After initial argument validation, the core zeros `*out` before subsequent
failure checks. An argument error, including a typed wrapper's early argument
error, may leave it unchanged. Only use the output when the return is `WL_OK`.
Failed encoding may have written unused snapshot bytes; it admits no work and
does not consume a call generation/operation.

**Owner constraint.** Allowed on the owner, including from handlers and terminal
callbacks. Use generated typed start functions rather than manually erasing a
callback type. For the demo, with `endpoint`, `now` and `completed` already set:

```c
request_value_t request = {.has_input = true, .input = INT32_MIN};
wl_kernel_call_handle_t handle;
wl_err_t error = demo_kernel_execute_start(&endpoint, &request, now, 500U,
                                          completed, NULL, &handle);
/* On WL_OK, request is no longer borrowed; completion needs owner steps. */
```

### Terminal results

Each accepted call delivers exactly one terminal as the owner continues
stepping. The handle becomes invalid before callback entry. A successful
cancel invalidates it for further cancellation even before terminal delivery.

| `outcome` | `status` / typed view |
| --- | --- |
| `WL_KERNEL_SUCCESS` | `0`; nonnull response view from generated completion |
| `WL_KERNEL_REJECTED` | Remote nonzero signed business status; null view |
| `WL_KERNEL_LOCAL_ERROR` | Local `wl_err_t`, e.g. `WL_ERR_IO`, `WL_ERR_ID_EXHAUSTED` or response decode `WL_ERR_CORRUPT_PAYLOAD`; null view |
| `WL_KERNEL_TIMED_OUT` | `WL_ERR_TIMEOUT`; null view |
| `WL_KERNEL_CANCELLED` | `WL_ERR_CANCELLED`; null view |
| `WL_KERNEL_CLOSED` | `WL_ERR_INVALID_STATE`; null view |

A malformed RPC metadata packet is a route error, not automatically a terminal
for a call. A matched success body that fails generated decoding *does* deliver
`LOCAL_ERROR / CORRUPT_PAYLOAD`. A valid unmatched or stale response is ignored.
Timeout/cancel cannot undo remote side effects; retrying uses a new operation.

The response struct and all views into payload storage are borrowed until
callback return. Copy with public `*_value_from_view` if retaining the result.
The [owned-result example](../examples/02_device_info/client.c) uses the
[shared completion helper](../examples/common/loopback_owner.h) for this purpose.

## Cancel a call

```c
wl_err_t wl_kernel_cancel(wl_kernel_t *kernel, wl_kernel_call_handle_t handle);
```

Marks a matching waiting call `CANCELLED`; `step` delivers its callback later.
It sends no cancellation packet and does not stop physical TX. A queued,
unleased request is removed when the terminal is delivered; leased bytes
remain until physical drain.

Returns `WL_OK` for a matching waiting handle, `WL_ERR_INVALID_ARG` for null
kernel, or `WL_ERR_NOT_FOUND` for wrong instance/slot/generation/operation,
already-terminal or already-completed work. Repeating cancel does not schedule
another callback. Owner-only, including from callbacks. Minimal use:
`error = wl_kernel_cancel(&endpoint.core, handle);`, then keep stepping.

## Admit an ordinary message

```c
wl_err_t wl_kernel_send(wl_kernel_t *kernel, uint16_t message_id,
                         const void *value, wl_kernel_encode_fn encode);
```

Encodes input synchronously into the single ordinary snapshot and queues it
before `WL_OK`. The source is then reusable, including view-backed bytes.
There is no ordinary-send terminal callback. It returns `WL_ERR_INVALID_ARG`
for null kernel/encoder or zero ID, `WL_ERR_INVALID_STATE` after close,
`WL_ERR_WOULD_BLOCK` while the snapshot is occupied, or the encoding/frame
errors described for call admission. Failure admits nothing. Later TX failure
is reported through `on_error` when configured.

Owner-only, including from callbacks. Minimal typed use:
`error = demo_kernel_send_event(&endpoint, &event);` or
`demo_kernel_send_event_view(&endpoint, &borrowed_event)`. Both copy by encoding
before returning. See [ordinary-message flow](kernel.md#follow-an-ordinary-message).

## Advance owner work

```c
wl_err_t wl_kernel_step(wl_kernel_t *kernel, uint32_t now, unsigned budget);
```

`budget` is `1..16` events. A completion batch, terminal delivery, TX attempt or
RX attempt each uses one event; idle checks do not. `now` is one snapshot used
throughout this call. Physical completion precedes eligible terminals, which
precede RX. A generation snapshot excludes new callback-created calls from this
step's terminal scan. Multiple due calls are delivered before RX up to the
remaining budget. RX/TX turns persist across passes, including `budget=1`.

Deadline equality expires: `(uint32_t)(now - deadline) <= INT32_MAX`. This wins
over a response still queued at that snapshot. Use consistent monotonic modular
ticks, timeout `1..INT32_MAX`, and service calls within the modular half-range.
See [time and scheduling](kernel.md#scheduling-uses-events-and-caller-time).

Returns `WL_OK` when no error is reported by the performed work,
`WL_ERR_INVALID_ARG` for null kernel or invalid budget,
`WL_ERR_REENTRANT` for recursive step, or an observed I/O/route/frame error.
An error does not roll back already performed work or close the kernel.
`on_error` reports route and TX errors on the owner when configured; do not
interpret the single return value as an exhaustive event log. A normal step
may deliver several callbacks, or none. Continue stepping after close to finish
shutdown. Never run step concurrently or recursively from a callback.

Minimal use: `error = wl_kernel_step(&endpoint.core, now, WL_KERNEL_BUDGET);`.
`demo_kernel_step(&endpoint, now)` uses that maximum budget.

## Query readiness and deadline

```c
int wl_kernel_ready(const wl_kernel_t *kernel, uint32_t now,
                     int *has_deadline, uint32_t *deadline);
```

Owner-only; all pointers must be valid. Returns nonzero for pending completion,
eligible terminal, runnable RX or runnable queued TX. RX blocked by reply
pressure is not runnable until capacity returns (or the kernel closes). TX
blocked by BUSY needs port activity; active physical TX prevents another submit.

Always sets `*has_deadline`; when nonzero, `*deadline` is the earliest future
client deadline. Otherwise `*deadline` is unspecified. An expired call makes
the return runnable rather than contributing a future deadline. Runnability
and a future deadline can both be present for different work. The function
neither consumes activity nor guarantees that no producer will publish next.

```c
int has_deadline;
uint32_t deadline;
int runnable = wl_kernel_ready(&endpoint.core, now, &has_deadline, &deadline);
/* Merge with adapter/application readiness; use a latched wake before wait. */
```

See [race-free waiting](kernel.md#wait-without-losing-activity) for the outer-loop
contract. `deadline - now` is a relative delay only for the returned future
deadline and the same time sample; rebase after additional work.

## Close and query quiescence

```c
wl_err_t wl_kernel_close(wl_kernel_t *kernel);
int wl_kernel_quiescent(const wl_kernel_t *kernel);
```

`close` is owner-only, outside step/callbacks. It stops the port, rejects new
admission, drops queued/unleased output and marks waiting calls `CLOSED`.
Already marked terminals retain their outcome; leased output remains alive.
It returns `WL_OK` (also on repeated close), `WL_ERR_INVALID_ARG` for null kernel
or `WL_ERR_REENTRANT` during step. It never invokes terminal callbacks inline.

`quiescent` requires a valid kernel on the owner. It returns nonzero only when
closed, not in step, all call slots free and the port quiescent. It does not
acknowledge driver stop, join external workers or prove callback exit.

Minimal sequence: `wl_kernel_close(&endpoint.core)`, stop/join the adapter by
its actual contract, keep stepping through final completion, then require
`wl_kernel_quiescent(&endpoint.core)` **and** adapter stop acknowledgement
before freeing storage. See [shutdown order](kernel.md#close-finish-io-then-release-storage).

## Callback interfaces

```c
typedef wl_err_t (*wl_kernel_encode_fn)(const void *value, uint8_t *data,
                                       size_t capacity, size_t *length);
typedef void (*wl_kernel_complete_fn)(wl_kernel_t *kernel,
    const wl_kernel_call_t *call, wl_kernel_result_t result,
    const uint8_t *body, size_t length);
typedef wl_err_t (*wl_kernel_route_fn)(wl_kernel_t *kernel,
                                      const wl_kernel_frame_t *frame);
typedef void (*wl_kernel_error_fn)(void *user, uint16_t message_id, wl_err_t error);
```

- **Encode:** owner-synchronous; writes no more than capacity, sets length on
  success, retains no input/output pointer. Low-level encoder errors propagate.
- **Complete:** generated glue receives a saved call descriptor, decodes a
  success body once and converts the erased callback back to its original
  function-pointer type before invoking it. It never calls through the erased
  type. The saved descriptor and body are borrowed for this invocation only.
- **Route:** owner-step scoped; dispatches the borrowed frame. `WL_OK` consumes
  RX. `WL_ERR_WOULD_BLOCK` retains it for retry: return this only before handler
  effects, with no reply reservation retained. Other errors release RX and are
  reported. Generated routes reserve before business decode/handler effects.
- **Error:** receives the configured context, message ID (possibly zero when
  packet parsing failed) and local error. It does not imply a business rejection
  or itself complete an outbound call.

Handlers/completions/error callbacks may send, start or cancel calls on the
same owner. Recursive step/close is rejected. Keep callbacks nonblocking; an
owner callback must not join/destroy its own runtime. The
[generated demo route](../examples/kernel/generated/demo_kernel.c) shows all four
interfaces together.

## Generated-route operations

These are owner-step helpers for generated dispatch, not application late-reply
tokens. Use the synchronous generated handler and application jobs for long work.

```c
wl_err_t wl_kernel_reply_reserve(wl_kernel_t *kernel, unsigned *index);
void wl_kernel_reply_abort(wl_kernel_t *kernel, unsigned index);
wl_err_t wl_kernel_reply_finish(wl_kernel_t *kernel, unsigned index,
    uint16_t response_id, const wl_kernel_rpc_metadata_t *request,
    int32_t status, const void *value, wl_kernel_encode_fn encode);
wl_err_t wl_kernel_response(wl_kernel_t *kernel, uint16_t response_id,
                            const uint8_t *data, size_t length);
```

**Reserve:** call inside step before business decoding or handler effects.
`WL_OK` marks a reply snapshot building and writes its index (`4..7`).
`WL_ERR_WOULD_BLOCK` means no reply storage; retain RX without running the
handler. Null kernel/output, outside step or closed kernel returns
`WL_ERR_INVALID_STATE`. On failure the output index is not usable.

**Abort:** after a successful reserve, release it when decoding fails. Requires
valid kernel and owner-step context; a reply index not currently building is
ignored. No callback is produced. Do not retain indices across route calls.

**Finish:** use the reserved index and validated request metadata; response ID
must be nonzero. `status=0` encodes `value` with nonnull `encode`; nonzero status
encodes rejection without invoking the encoder or adding business bytes.
`WL_OK` queues an owned reply snapshot and ends the reservation. Metadata and
value are no longer borrowed after return. Null kernel/request, invalid ID or
index, outside step, non-building index, or null encoder on success returns
`WL_ERR_INVALID_ARG`. Argument failures do not release a valid reservation;
the route must abort it. Metadata/encoding/frame errors release it and return
the error. No automatic error response is generated.

**Response:** dispatch a response-ID payload during owner step. Outside step
returns `WL_ERR_INVALID_STATE`; malformed metadata returns its parse error.
Valid wrong-session, unsent, nonwaiting or unmatched operation/response-ID
packets return `WL_OK` without callback. A match releases the logical call and
invokes completion within step, with success/rejection and borrowed body.

Minimal route pattern: validate metadata, reserve, decode, abort on decode
failure, run handler, finish; response cases call `wl_kernel_response`.
The [checked-in route](../examples/kernel/generated/demo_kernel.c) is a complete
implementation. [`kernel.c` tests](../tests/core/kernel.c) cover reservation
pressure, duplicate requests and reply-encoding failure after effects.

## Initialize a packet port

```c
wl_err_t wl_packet_init(wl_packet_port_t *port, const wl_packet_config_t *config);
size_t wl_packet_rx_unit_size(const wl_packet_port_t *port);
```

`config` supplies a fresh nonzero `instance`, `rx_storage`, `rx_unit_size`,
`rx_storage_size`, nonnull `submit`, optional `notify` and shared `user`.
Storage must hold at least two units. Each unit must hold the largest packet
the connector may publish; physical adapters may need additional headroom.
Init copies configuration descriptors and starts port admission, but does not
start the driver. Backing storage/context remain caller-owned.

Returns `WL_ERR_INVALID_ARG` for null port/config/RX data/submit, zero instance
or unit size, overflow of two units or insufficient total storage.
`WL_ERR_NOT_SUPPORTED` means required C atomics are not lock-free; the port is
not running and cannot be used as successfully initialized. Otherwise returns
`WL_OK`. Init/reinit must not race I/O or erase a live claim, borrow or transfer.

`wl_packet_rx_unit_size` returns configured unit size, or `0` for null port;
it does not claim space. Minimal initialization is shown in
[`bare_metal_loopback.c`](../examples/bare_metal_loopback.c); after init,
`wl_packet_rx_unit_size(&port)` queries its capacity.

## Produce RX: claim, commit or abort

```c
wl_err_t wl_packet_rx_claim(wl_packet_port_t *port, size_t maximum,
                            wl_packet_claim_t *out);
wl_err_t wl_packet_rx_commit(wl_packet_port_t *port,
                             const wl_packet_claim_t *claim, size_t length);
wl_err_t wl_packet_rx_abort(wl_packet_port_t *port,
                            const wl_packet_claim_t *claim);
```

One serialized RX producer owns this sequence. **Claim** returns writable
`out->span` of exactly `maximum` bytes and a fresh token. Write only within it;
no owner can see it before commit. The original saved capacity is authoritative
even if a caller edits its local claim descriptor.

| Claim result | Meaning |
| --- | --- |
| `WL_OK` | Producer owns one writable claim |
| `WL_ERR_INVALID_ARG` | Null port/output or zero maximum |
| `WL_ERR_INVALID_STATE` | Port stopped |
| `WL_ERR_BUSY` | A producer claim is already active |
| `WL_ERR_BUF_TOO_SMALL` | Maximum exceeds RX unit size |
| `WL_ERR_WOULD_BLOCK` | Both units occupied |
| `WL_ERR_ID_EXHAUSTED` | Claim identity exhausted |

After basic argument validation, claim zeros the output before other checks;
use it only on `WL_OK`. **Commit** requires `1..claimed maximum` bytes. It
publishes length/data with release ordering and signals activity. `WL_OK`
ends producer ownership; no writes are allowed until another claim. **Abort**
releases a matching claim without publishing input and signals activity.

Commit/abort return `WL_ERR_INVALID_ARG` for null port/claim, and
`WL_ERR_NOT_FOUND` for a stale or mismatched instance/sequence/data pointer.
Commit also returns `WL_ERR_INVALID_ARG` for zero/oversized length, leaving the
claim active for correction or abort. A valid commit after stop discards the
claim, signals activity and returns `WL_ERR_INVALID_STATE`; it does not publish
a packet. Abort remains allowed after stop. A completed claim cannot be reused.

Minimal producer use, with nonzero `length` and readable `source`:

```c
wl_packet_claim_t claim;
wl_err_t error = wl_packet_rx_claim(&port, length, &claim);
if (error == WL_OK) {
  memcpy(claim.span.data, source, length);
  error = wl_packet_rx_commit(&port, &claim, length);
  /* If a validation error leaves the claim active, correct it or abort it. */
}
```

The [loopback service](../src/packet_loopback.c) also shows abort on failure.

## Consume RX and query space

```c
wl_err_t wl_packet_rx_borrow(wl_packet_port_t *port, wl_packet_borrow_t *out);
wl_err_t wl_packet_rx_release(wl_packet_port_t *port,
                              const wl_packet_borrow_t *borrow);
int wl_packet_rx_ready(const wl_packet_port_t *port);
int wl_packet_rx_available(const wl_packet_port_t *port);
```

**Borrow** is owner-only and returns the oldest committed packet's const
data/length/token. `WL_OK` starts a borrow; `WL_ERR_INVALID_ARG` means null
port/output, `WL_ERR_BUSY` an existing owner borrow, and `WL_ERR_NO_DATA` an
empty queue. Failure does not produce a new borrow. The producer cannot reuse
its unit until **release**, which validates instance/sequence/data pointer,
advances RX and signals activity. Release returns `WL_OK`,
`WL_ERR_INVALID_ARG` for null arguments or `WL_ERR_NOT_FOUND` for a noncurrent
borrow. Neither operation requires the port to be running: drain committed
input after stop. Never release a borrow while users still read its bytes.

`rx_ready` is an owner query for committed input (including the current borrow).
`rx_available` is a producer/owner hint: false when stopped, a claim is active
or both units are occupied. True does not reserve a unit; the next claim is
authoritative. Both need valid initialized ports and return boolean values.

Minimal owner pattern: if `wl_packet_rx_ready(&port)`, borrow, process the
borrow synchronously, then `wl_packet_rx_release(&port, &borrow)`. A kernel
already owns this consumer side; an adapter must not borrow behind its back.
See [`packet_port.c` tests](../tests/core/packet_port.c) for ready/available and
stale-release cases.

## Submit physical TX

```c
typedef wl_packet_submit_t (*wl_packet_sink_fn)(void *user,
    const uint8_t *data, size_t length, wl_packet_token_t token);
wl_err_t wl_packet_tx_submit(wl_packet_port_t *port, const uint8_t *data,
                             size_t length, wl_packet_token_t *out);
```

Owner-only. Requires nonnull port/data/output and nonzero length. The port
establishes an active token before calling the configured sink synchronously:
inline `wl_packet_tx_publish` is supported. The sink must return one of:

| Sink result | Acceptance and future obligation |
| --- | --- |
| `WL_PACKET_SENT` | Source consumed **and** transfer physically drained; port publishes DRAINED automatically |
| `WL_PACKET_STARTED` | Transfer accepted; retain source until consumption, publish completion using this token, possibly before returning |
| `WL_PACKET_BUSY` | Nothing accepted or retained; no completion allowed; publish activity when resources become available |
| `WL_PACKET_FAILED` | Nothing accepted or retained; no later completion allowed |

Port return is `WL_OK` with a valid `*out` for SENT/STARTED,
`WL_ERR_WOULD_BLOCK` for active TX or sink BUSY, `WL_ERR_IO` for sink FAILED,
`WL_ERR_INVALID_STATE` after stop, `WL_ERR_ID_EXHAUSTED` at TX identity
exhaustion, or `WL_ERR_INVALID_ARG` for invalid arguments. Failure does not
write a new output token. Do not overwrite accepted source memory until owner
completion processing observes CONSUMED. No second transfer starts before
DRAINED has been taken by the owner, even if the sink completed inline.

Minimal direct-port use: submit a live source, retain its token on `WL_OK`,
then take completion below. In a kernel integration, the kernel already owns
TX submission/take; the adapter implements the sink and publication, not a
second submitter. See [TX fixture](../tests/core/packet_port.c).

## Publish and take TX completion

```c
wl_err_t wl_packet_tx_publish(wl_packet_port_t *port,
                              wl_packet_token_t token, unsigned flags);
unsigned wl_packet_tx_take(wl_packet_port_t *port, wl_packet_token_t *out);
int wl_packet_tx_active(const wl_packet_port_t *port);
int wl_packet_tx_ready(const wl_packet_port_t *port);
```

**Publish** runs in I/O context (also allowed inline from the sink). Flags may
be ORed. They describe actual lifetime boundaries, not logical RPC state:

| Flag | Meaning |
| --- | --- |
| `WL_PACKET_CONSUMED = 2` | Adapter no longer reads source bytes |
| `WL_PACKET_DRAINED = 4` | Physical transfer ended; implies CONSUMED |
| `WL_PACKET_TX_ERROR = 8` | Transfer ended with failure/cancellation and no source use remains; implies DRAINED and CONSUMED |

Returns `WL_OK` for new bits on the active matching token,
`WL_ERR_INVALID_ARG` for null port, zero flags or unknown bits, or
`WL_ERR_NOT_FOUND` for wrong instance/sequence, inactive/already-drained
transfer or a publication adding no bits. Sequence and flags occupy one
lock-free atomic word: a stale publisher cannot attach completion to a newer
transfer. Stop does not reject valid final completion for an existing TX.

**Take** is owner-only. Supply valid port/output. It returns each new bit once,
writing the matching token only for a nonzero result; `0` means no new bits and
leaves output unchanged. Taking DRAINED ends the port's active transfer. If
CONSUMED was taken earlier, a later DRAINED take need not return CONSUMED again.
The kernel conservatively keeps its leased snapshot until DRAINED.

`tx_active` needs a valid port and reports physical-port occupancy, including
published DRAINED not yet taken. It is safe as an atomic status observation;
it does not itself end a lease. `tx_ready` is owner-only and reports whether
untaken completion bits exist. Both return boolean values and cause no progress.

Minimal publisher use after real drain:
`wl_packet_tx_publish(&port, token, WL_PACKET_DRAINED)`.
Minimal direct owner use: `flags = wl_packet_tx_take(&port, &token)`, release
source on CONSUMED and permit the next TX after DRAINED. In kernel use, keep
stepping instead. The [port tests](../tests/core/packet_port.c) cover split and
inline completion; the [concurrent fixture](../tests/core/packet_port_concurrent.c)
races old completions against new submissions.

## Activity and wake notification

```c
typedef void (*wl_packet_notify_fn)(void *user);
void wl_packet_activity(wl_packet_port_t *port);
int wl_packet_take_activity(wl_packet_port_t *port);
int wl_packet_activity_pending(const wl_packet_port_t *port);
```

All need valid initialized ports. `activity` can run on producer/I/O or owner;
it latches the activity bit and invokes configured `notify(user)`, if any.
Notify may run in I/O context, must be nonblocking and must not reenter owner
operations. RX commit/abort/release, accepted TX publication and stop already
signal activity. A sink that returned BUSY must signal resource availability;
otherwise the kernel intentionally leaves its TX retry blocked.

`take_activity` is the owner consumer: atomically clears/returns the previously
latched bit. The kernel uses it at step entry to re-enable TX retries; an outer
runtime must not steal it from that consumer. `activity_pending` is a
nondestructive atomic observation available to producer/owner. These are
coalesced hints, not event counts, application callbacks or physical completion.

Minimal adapter use when a BUSY resource returns:
`wl_packet_activity(&port)`. Wire `notify` to a latched external event, and
combine `activity_pending`/readiness with that event across query/wait.
See [waiting](kernel.md#wait-without-losing-activity).

## Stop a port and establish silence

```c
void wl_packet_stop(wl_packet_port_t *port);
int wl_packet_quiescent(const wl_packet_port_t *port);
```

`stop` is owner-only and requires a valid port; kernel close also invokes it.
It atomically stops new claims/submits and signals activity; repeated stop is
allowed. It does not cancel a driver, abort a live claim, consume queued RX or
take TX completion. A producer already holding a claim must commit/discard or
abort it, and an active TX still requires real final publication. Coordinate
stop with adapter shutdown; it is not a substitute for producer exit.

`quiescent` is an owner query requiring stopped admission, no active claim,
no borrow, no queued RX and no active TX. It returns boolean, not an error.
It cannot prove a notification callback or driver worker has exited. Keep
storage/context alive until adapter stop/join acknowledgement as well. With a
kernel, use `wl_kernel_quiescent` for the additional logical-call requirement.

Minimal direct-port shutdown: stop, synchronize producers and finish their
claims/transfers, release all borrowed/queued RX, take final TX completion,
then check quiescence before reconstruction with a fresh instance. The
[stop/reinit fixture](../tests/core/packet_port.c) exercises final completion
after stop, fresh instances and identity exhaustion.
