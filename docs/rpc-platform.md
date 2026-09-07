# Synchronous RPC and platform waiting

Start with the [calculator tutorial](tutorial-rpc.md). This page covers platform
integration, background ownership and custom clocks. [中文](rpc-platform-cn.md).
Codegen ABI 24 changes local APIs, not framing or RPC wire format.

## One entry, two ownership modes

```c
wl_rpc_completion_t result = calculator_endpoint_add_sync(
    &client, &request, &response, 1500U);
```

Without an executor binding, the caller owns the endpoint: generated step,
event/deadline wait, then another step. UDP attachment installs its waiter.
Other platforms install `wl_endpoint_set_waiter(handle, &waiter)` during setup.
A missing waiter returns FAILED / `local_error=WL_ERR_NOT_SUPPORTED`; no busy fallback.

Waiting must not parse, dispatch callbacks or drive the endpoint. WL_OK and NO_DATA
trigger another state check; other errors detach this call's internal notification
before returning, without closing unrelated calls. Only success updates the owned
response; it remains valid after endpoint close.

## Background ownership: proxy rather than a second owner

```cpp
wirelink::host::Executor executor; // Outlives the endpoint and all callers.
calculator_endpoint_t client{};
// Initialize client, attach UDP, use thread-safe monotonic_clock().
CHECK(executor.initialize(calculator_endpoint_driver(&client)) == WL_OK);
CHECK(executor.start() == WL_OK);
// Business threads still call calculator_endpoint_add_sync(...).
// They must not call step, async, cancel or close directly.
executor.stop(); // Stop admission, close on owner, notify callers, join owner.
// Join every calling thread before releasing endpoint / adapter / executor.
```

The generated synchronous entry automatically uses the proxy. Eight bounded proxy
positions reference blocked callers; actual RPC capacity still belongs to the endpoint.
Full capacity returns BUSY, not an unbounded queue. Only the owner submits, drives,
completes or closes RPCs. Sync from that owner's callbacks returns REENTRANT.
Configure handlers, waiter and binding before start; never replace them while running.
Keep both executor and adapter alive through stop and all caller joins. The binding
survives close so late callers receive CANCELLED without reading mutable endpoint state.

## One clock and deadline

Submission, proxy queuing and retransmission share the endpoint clock. Timeout must
be 1…INT32_MAX milliseconds; proxy jobs expire from admission, and expired unsent jobs
are never transmitted. Wait receives a relative maximum; UINT32_MAX means event-only
waiting. Notification must latch: notify-before-wait cannot be lost. Both UDP and
Zephyr waiters follow this rule and avoid fixed 1-ms idle polling.

Proxy admission samples the same clock on business threads, so its provider must be
thread-safe. A manual test clock should use atomic storage and notify after advancing.
A frozen clock, blocked owner or scheduling delay can extend wall-clock duration;
the timeout is not a hard-real-time preemption guarantee.

## Zephyr threads

Enable `CONFIG_WIRELINK_ZEPHYR_WAIT=y` and include `wirelink/zephyr/wait.h`:

```c
static wl_zephyr_waiter_t activity;
wl_zephyr_waiter_init(&activity);
wl_waiter_t waiter = wl_zephyr_waiter_descriptor(&activity);
wl_endpoint_set_waiter(calculator_endpoint_handle(&client), &waiter);
```

Use `wl_zephyr_monotonic_clock()` if appropriate. RX/TX-completion activity calls
`wl_zephyr_waiter_notify(&activity)`, including from ISR; sync itself is thread-only.
`wl_zephyr_waiter_stop` latches cancellation and wakes an unbounded wait. The owner
then closes the endpoint. No thread or heap is created. DMA memory and IRQ quiescence
remain adapter responsibilities.

## Results and shutdown

`status` is a terminal outcome; `rejection` is business-only. `local_error` describes
admission/platform failures, while transport/runtime/codec fields preserve protocol
diagnostics. Normal timeout is not a local platform error. Cancellation and timeout
do not undo remote effects.

An owner-side waiter can return CANCELLED to stop. For background mode, requestStop,
then stop from a non-owner thread. Never synchronously destroy an object in its own
callback. The tutorial client uses a finite 1500-ms budget; it does not promise
immediate Ctrl+C interruption during a synchronous wait.
