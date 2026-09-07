# Configure a clock once

Wirelink needs elapsed time to retry unacknowledged packets and expire RPC work.
It does not need the calendar date or synchronized clocks on both devices.
ABI 21 moves this input from every default endpoint operation to initialization.
[中文](endpoint-clock-cn.md).

## Ordinary applications

Use `wl_platform_environment()` for the default clock and automatic identity
source. Link `Wirelink::platform` on desktop or enable `CONFIG_WIRELINK_PLATFORM`
on Zephyr. Existing products can override only the clock:

```c
static wl_time_ms_t read_uptime(void *user) {
  (void)user;
  return k_uptime_get_32(); /* Zephyr; include <zephyr/kernel.h>. */
}

static telemetry_endpoint_t endpoint;
wl_environment_t environment = wl_platform_environment(); /* wirelink/platform.h */
environment.clock = (wl_clock_t){read_uptime, NULL};
int result = telemetry_endpoint_init(&endpoint, environment);
```

For configurable initialization, fill `config.environment.clock` after
`endpoint_config_defaults()`. A null clock function is rejected; custom boards
can inject an [environment](session.md). Then use `step(endpoint)` and ordinary
`*_async(..., timeout_ms, callback, context, optional_call)` without `now_ms`.
Immediate handlers fill responses directly. Advanced `call(..., timeout_ms, &call)`,
`complete(..., &response)`, and `reject(..., status)` without `now_ms`.
Application publishing intervals are still application policy.

## Ownership and cost

- The function returns local monotonic milliseconds modulo 2^32. Do not use wall
  time or reset the epoch while an endpoint is alive. Timed intervals are below
  2^31 ms; observe/drive the system within that wrap-safe horizon.
- The descriptor is copied; `user_data` is borrowed through endpoint close.
  Reads happen on the communication owner, not RX interrupts. The callback must
  be quick, nonblocking, non-reentrant, and must not throw across C.
- One step reads once, before adapter completion service. Replies made inside
  handlers reuse that snapshot. Keep handlers bounded; a deferred reply issued
  later reads fresh time. An outside reliable submission reads once for both
  link and RPC timing. No initial step or hidden poll is needed to prime time.
- An outside hint query reads once but does not advance state. Inspection,
  release, retained reads, initialization/close, and unreliable typed sends do
  not read the clock. There is no global clock or live clock replacement API.

The allocation-free core never calls an OS clock. The optional C++ executor
defaults to the native provider and accepts an override at `initialize()`;
its clock context must survive `stop()`.

## Advanced API migration

Rebuild core, generated code, and consumers together; do not mix generated ABI
headers or endpoint layouts. Clock injection arrived in ABI 21 without changing
wire bytes. Current ABI 26 also introduces [automatic sessions and managed RPC v2](session.md),
which requires paired managed-RPC peer upgrades.

| API | Current use |
| --- | --- |
| Current generated default init | `init(endpoint, environment)` or `config.environment.clock` |
| Generated step / RPC / generic hint | Remove explicit `now_ms` |
| `wl_send_reliable` / `wl_tx_payload_commit` | Add `now_ms` before `out_handle` |
| Codec binding `*_send` | Add `now_ms` after `delivery`; ignored for unreliable |
| Advanced runtime / `wl_poll` / raw pump | Continue supplying one explicit clock domain |

Do not mix a default endpoint's event/terminal ownership with a separate raw
pump. Advanced adapters deliver async completions in owner-side pump service,
after the current time has been established. Timed delivered RPC cache entries
are also reclaimed during request admission, so an idle wake does not require
a preliminary poll; undelivered responses remain protected.

## Bindings and validation

[`tests/clock_bridge`](../tests/clock_bridge/) validates a native-owned generated
endpoint through exported C from C++ and Python `ctypes`. Python chooses a native
provider or advances a manual test clock; it does not supply a per-read Python
callback. The bridge is a test fixture, not a released Python SDK.

The benchmark compares raw idle pump, default endpoint with manual/native clock,
and unreliable typed submission. Run `wirelink_clock_benchmark` from a Release
tutorial build; report compiler/CPU and both wall/CPU time. Its paths do different
amounts of work, so differences are not a pure function-pointer cost or an
end-to-end network latency claim. Clock-read budgets are checked independently.
[`samples/zephyr/endpoint_clock`](../samples/zephyr/endpoint_clock/) provides the
separate firmware correctness/idle-cycle test. See [evolution status](endpoint-clock-evolution.md).
