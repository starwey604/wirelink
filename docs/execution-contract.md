# Execution and waiting (development contract 0.12)

Rebuild the C library, WLC output, adapters and callers together. Contract 0.9
changes execution interfaces and layouts; the frozen protocol-v1 vectors and
codec bytes are unchanged by this refactor.

## One bounded owner pass

`wl_readiness_t` describes current state: `runnable` permits a bounded transition,
`blocked` describes outstanding work waiting for input/resources/time, and
`next_deadline_ms` is a relative delay. `WL_NO_DEADLINE` means no timer. A zero
deadline is runnable. Merge readiness by OR-ing flags and taking the minimum
deadline. Queries do not submit, cancel, expire or dispatch work.

`wl_pump_result_t.did_work` records work done in the preceding pass. It never
requests another pass. `polls` and `services` count actual calls in that pass,
including protocol work that produces no application event. A complete RX frame,
published completion, writable queue entry or unreported terminal callback can
be runnable. A nonempty queue behind a BUSY sink is blocked.

An owner calls `step`, queries merged readiness, and waits only when runnable is
clear. Cap the wait by the next business deadline (publishing, shutdown, etc.).
All clock-dependent work in a pass uses one snapshot taken before adapter
completion processing. An external query takes a fresh snapshot; a manually
advanced clock must notify the waiter. No producer may mutate RPC/protocol state.

Adapter service and application step require their matching full readiness
callbacks. The pump gates them separately. There is no legacy unconditional
service path or executor follow-up flag. The core still permits explicit bounded
manual polling, for simulations and applications with an actual polling source.

## Static services

`wl_endpoint_set_services` borrows an immutable array (maximum 64). A service
with a step receives initial activation; subsequently it runs only when active
or its real timer is due. Its `wl_work_result_t` reports `did_work` separately
from further `runnable` work. Explicit owner-local work must call
`wl_endpoint_activate_service` or `wl_endpoint_activate_policy`, which also
notifies the installed waiter. Publish cross-thread jobs through an executor or
synchronized queue first; activation does not authorize concurrent core access.

RX/terminal events and changes to the core resource generation activate bound
services. The generation changes at TX consumed/drained/writable, terminal take,
RX release and claim abort. It is owner-only, with no atomic producer writes.
Services still have a bounded deadline-query scan; no deadline heap is introduced.

## Notification and close

Publish input before notifying. Notifications are latched or level-triggered,
including between a readiness check and wait. UART IRQ/DMA use a semaphore of
capacity one. Service acknowledges its latch before reading published state;
arrivals afterward leave a fresh latch or are observed by the ensuing core poll.
The host executor uses its existing generation and coalescing event. UDP also
checks socket level readiness so RX is visible while continuous TX keeps the
owner from entering wait. Mask socket RX while receive slots are full.

IRQ publishes RX commits, errors, pause and TX completion immediately, then
notifies at most once per valid callback, after its bounded 32-iteration loop
and before releasing the binding lock. Every valid exit joins this point. No
publication means no notification. There is no cross-callback pending flag or
semaphore-count test; a later callback can always give a fresh latch. Optional
notifiers follow the same batching rule. Notifications are not event counts.

Acknowledge only before inspecting the state covered by that latch. After wait
consumes a latch, drive/query again; never reset it after checking readiness.
An RX-only pass may skip adapter service, leaving one finite stale wake. Partial
RX must not re-latch itself into an empty-pass loop. External queues sharing the
waiter must participate in the owner's final readiness check, or use a generation
handshake as the host executor does. Publish through the synchronized queue, then
notify; do not mutate endpoint/RPC state from another thread.

`wl_endpoint_get_readiness_at(endpoint, sampled_at_ms, &hint)` queries at an
explicit snapshot in the endpoint's clock domain, without sampling a clock or
expiring work. At waiter entry, rebase its relative deadline with
`wl_wait_remaining(hint.next_deadline_ms, sampled_at_ms, wait_now_ms)` and take
the minimum with the caller's business limit. A zero limit requests another
owner pass. The unbounded sentinel stays unbounded; all other limits stay finite.
Elapsed time between samples must be less than one full 32-bit clock wrap.
The automatically sampled `wl_endpoint_get_readiness` remains available for
callers consuming a hint immediately.

A Zephyr raw owner can save `k_uptime_ticks()` before querying readiness, then
call `wl_zephyr_timeout_since(maximum, sampled_ticks)` immediately before waiting.
It binds a relative kernel timeout to that snapshot and preserves absolute
deadlines, zero and forever. On 64-bit timeout kernels it returns an absolute
deadline, so the remaining duration is not renewed after returning. Use
`wl_zephyr_timeout_us_since` for a microsecond allowance; `UINT32_MAX` means
forever and finite conversion uses 64-bit ticks first. Both are declared in
`<wirelink/zephyr/timeout.h>` and implemented outside the caller in the platform
runtime, without acknowledging notifications or driving protocol work. On
32-bit timeout kernels elapsed ticks are subtracted; the sampling interval must
be shorter than one complete tick wrap. Kernel scheduling and tick rounding
still apply.

Zephyr converts milliseconds to ticks in 64 bits before narrowing. A 32-bit
timeout kernel may wake early for a very long finite limit, allowing the owner
to recompute; it must never turn that limit into `K_FOREVER`. Kernel tick rounding
and scheduling latency do not extend a logical RPC deadline. Public DMA wait
checks cancellation before and after waiting. Quiesce separately waits for real
buffer returns and callback exit; cancelled business waiting does not return
driver-owned memory.

IRQ optional notification callbacks are ISR-safe, nonblocking and cannot reenter
the adapter. Zephyr UDP notifications are thread-only. Astrial notifications run
on its I/O thread. Producer contexts and waiters must outlive quiescence; join
external producer threads before destroying them.

IRQ callback registration uses stable generation-tagged bindings. Stop removes
the binding under the same lock used before an ISR dereferences adapter storage.
DMA bindings also count callbacks in flight; quiesce waits for driver TX/RX
ownership and those callbacks before detaching notification storage. A driver
that never returns aborted buffers cannot provide completed synchronous close;
the caller must not free those buffers merely because a timeout elapsed.

## TX memory and line state

`wl_tx_consumed(ctx, token, result)` ends the adapter's source-memory lease.
FIFO copy and DMA DONE/ABORT have this meaning. Neither means peer delivery,
link ACK or RPC success. A reliable transaction retains its encoded image until
its logical result is taken. Stale/duplicate completion tokens return NOT_FOUND.

`WL_SINK_SENT` ends memory access synchronously; when physical drain is
configured it also guarantees that observable boundary has completed. A sink
that cannot provide this synchronously returns `WL_SINK_STARTED` and publishes
the two owner reports separately.

Set `wl_set_tx_drain_required` before sending only when physical idle is required.
Then `wl_tx_drained` is an independent, token-checked report. It gates new physical
submissions and the reliable ACK timer. The timer starts at the owner pass that
observes drain, never an inferred platform epoch. An early ACK/response may
finish logical work; memory release and physical serialization remain enforced.

IRQ always disables TX-empty at final FIFO copy. It never spins waiting for line
idle. Optional physical-idle queries use `drain_check_ms` (default 1 ms), only
while drain is pending. DMA uses the same bounded fallback when requested. A
missing/negative idle capability fails that drain; CDC does not fake line idle.
Driver BUSY requires a writable event; DMA/UDP use a documented retry deadline
when their driver lacks a separate writable callback.

## Borrowed RX and state caches

`WL_PUMP_EVENT_CONSUMED` means the callback discharged the event/handle.
`WL_PUMP_EVENT_LEASED` retains an RX lease: save the event, release it on the owner
when the business consumer has accepted its data, and release it before close.
Readiness blocks further RX parsing while that lease is held. `UNHANDLED` requests
the pump's default release/take.

The ring delimiter cache belongs only to the consumer. It caches a found
delimiter or the scanned prefix for a particular read cursor; consume invalidates
it, and newly acquired producer writes extend a negative search. Producers
publish cursors with release ordering and never write the cache. Overflow remains
separately latched and is processed even without a complete frame.

Generated runtime expiry runs when its actual timer is due. Response admission
checks the corresponding delivery resource; async terminal notifications and
retiring handles have separate readiness. A failed synchronous response submit
records the owner resource generation and stays blocked until that generation
changes; querying or manually servicing it again does not retry the same error.
The first failed attempt is did_work history. Timer queries still scan bounded RPC
slots and never expire entries. This refactor does not introduce a four-slot
index or change replay/response-acceptance rules.

See [the Chinese contract](execution-contract-cn.md) and the
[execution report](execution-refactor-round-1-result-cn.md) for validation and
performance acceptance status.
