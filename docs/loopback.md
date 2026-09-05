# Allocation-free loopback adapter

`Wirelink::loopback` connects two initialized native-packet links without an
OS, heap, thread, or additional payload buffer. It is the standard transport
for examples, application-runtime tests, and hardware-free bring-up.

```c
#include <wirelink/loopback.h>

wl_loopback_t transport;
wl_loopback_service_result_t step;

wl_loopback_init(&transport, &controller, &device);
wl_send_unreliable(&controller, STATUS_MESSAGE_ID, payload, payload_len);
wl_loopback_service(&transport, 4U, &step);
```

Both links must already be initialized with `WL_ENVELOPE_NATIVE_PACKET` and
the same integrity mode. The adapter takes exclusive ownership of their sink
bindings until `wl_loopback_quiesce()`.

## Progress and backpressure

Each direction has one asynchronous unit slot. The sink returns
`WL_SINK_STARTED` and borrows Wirelink's stable encoded unit; no second payload
copy is made. `wl_loopback_service()` attempts no more than `unit_budget`
deliveries and calls `wl_tx_complete()` only after a unit leaves its slot.

If the destination still owns an RX event, service returns
`WL_ERR_WOULD_BLOCK`, leaves the source unit borrowed, and increments the
destination's `rx_backpressure` counter. Release the event, then call service
again. A larger budget may carry reliable DATA and its generated ACK in one
bounded pass.

Call `wl_loopback_get_stats()` for either endpoint's common adapter counters.
`wl_loopback_reset_stats()` retains live lifecycle flags while clearing
counters. Quiesce before reinitializing either link; it unbinds both sinks and
fails any pending asynchronous unit deterministically.
