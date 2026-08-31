# Allocation-free SPSC FIFO

`wirelink/fifo.h` provides the application-layer `FIFO` delivery policy for
values whose order and multiplicity must be preserved. It is independent of
`wl_ctx_t`, allocates no memory, and does not change the Wirelink v1 frame.
The same primitive can carry received typed values toward an application task
or pre-encoded outbound commands toward Wirelink's single consumer.

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

## Typed decode and command queues

A WLC route can decode without an intermediate message copy: claim a slot,
point the generated route scratch at `claim.value`, then publish from the typed
handler. Any dispatch failure must abort the active claim. Fixed fields and
inline packed arrays are self-contained. Borrowed `bytes` and `string` fields
must be copied into slot-owned bounded backing before the Wirelink event is
released. Repeated fields likewise need distinct backing storage and capacity
for every FIFO slot.

For the reverse direction, an application can make the FIFO value an inline
command record containing delivery, message ID, payload length, and a bounded
encoded payload. The application producer encodes directly into the claimed
record and wakes the Wirelink consumer after publication. If the core reports
`WL_ERR_BUSY` or `WL_ERR_WOULD_BLOCK`, the consumer may keep the acquired head
and retry it once after transport or protocol progress; release only after the
command is accepted or definitively rejected. A result queue uses the same
FIFO API with the roles reversed.

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
