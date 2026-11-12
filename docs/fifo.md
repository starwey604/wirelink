# Allocation-free SPSC FIFO

[中文](fifo-cn.md)

`wirelink/fifo.h` provides an application queue preserving value order and
multiplicity, linked through `Wirelink::containers`. C owns its atomic context;
C++ uses opaque pointers. Queue owned receive values for a consumer or pending
commands for the owner; application code defines network routes and retries.

## Storage and initialization

The caller supplies the opaque context and a contiguous array of fixed-size
value slots. A requirements query validates capacity and alignment and reports
the rounded stride and complete storage size:

```c
struct control_event slots[8];
wl_fifo_t fifo;
wl_fifo_config_t config = {
    .value_size = sizeof(slots[0]),
    .value_alignment = _Alignof(struct control_event),
    .capacity = 8,
};
wl_fifo_requirements_t requirements;

wl_fifo_requirements(&config, &requirements);
wl_fifo_init(&fifo, &config,
             &(wl_fifo_storage_t){slots, sizeof(slots)});
```

Capacity must be nonzero and below `2^31`. The implementation uses wrapped
32-bit cursor subtraction, so this bound keeps full/empty classification
unambiguous across cursor wrap. Every C11 atomic used by the context is checked
with `atomic_is_lock_free()` during initialization. Unsupported targets receive
`WL_ERR_NOT_SUPPORTED` instead of a hidden library lock.

Initialization and reset require external quiescence. All other operations are
strictly SPSC: one producer owns the write lifecycle and one consumer owns the
read lifecycle. Multiple producers require one queue per producer or external
serialization; `volatile` is not synchronization.

## Borrowed ownership

The producer reserves the next slot with `wl_fifo_write_claim()`, fills the
returned value in place, then calls `wl_fifo_write_publish()` or
`wl_fifo_write_abort()`. A successful publish release-transfers the completed
slot to the consumer. The claim pointer is invalid after either finish call.

The consumer obtains the oldest published value with
`wl_fifo_read_acquire()`. Its pointer is immutable and remains valid until the
matching `wl_fifo_read_release()`. Release returns that slot to the producer;
the producer cannot overwrite a borrowed or unread value. Only one claim and
one view may be active per role, and opaque tokens reject stale or mismatched
finish calls.

When every slot is occupied, `wl_fifo_write_claim()` returns
`WL_ERR_QUEUE_FULL`, increments `full_rejections`, and leaves all queued values
unchanged. FIFO deliberately has no drop-oldest policy because producer-side
reclamation would violate consumer ownership. Use `LATEST` when freshness is
more important than multiplicity.

## Queue typed values and pending commands

In a generated ordinary-message callback, claim a slot and use
`*_value_from_view` to copy the borrowed message into a slot-owned `*_value_t`.
Check conversion before publishing; abort on conversion failure. Copy every
borrowed field before callback return. Configure slot size/alignment for the
owned type, rather than copying a view containing pointers. See the
[result-copy example](tutorial-rpc-values.md).

A full queue returns `WL_ERR_QUEUE_FULL`; the application chooses dropping,
recording or business flow control. This local rejection does not make the
kernel retry an ordinary received message. Standalone-codec repeated views need
per-slot backing and are not bounded kernel routes.

For outbound work, publish a message ID, owned request or bounded encoded
payload and wake the owner. The owner may retain the acquired head across
`WL_ERR_WOULD_BLOCK`, retrying after real resource progress. Release after
successful admission or definitive rejection. The [kernel model](kernel.md)
explains input snapshots and waiting.

## Reset and statistics

`wl_fifo_reset()` discards unread values and makes depth zero. It is rejected
while either role owns a claim/view and must not run concurrently with SPSC
activity. Reset preserves cumulative counters and the lifetime high-water mark.

`wl_fifo_get_stats()` is safe during conforming SPSC use. `depth` includes an
acquired but unreleased head. `high_watermark` is the greatest depth observed
by the producer. `publishes`, `consumes`, `full_rejections`, `empty_reads`,
`aborts`, `resets`, and `errors` saturate at `UINT32_MAX`. The fields are an
observational snapshot rather than one atomic transaction. Ordinary empty and
full results have dedicated counters and are not lifecycle errors.
