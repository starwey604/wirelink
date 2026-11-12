# Allocation-free LATEST mailbox

[中文](latest-mailbox-cn.md)

`wirelink/latest.h` provides application latest-value storage, linked through
`Wirelink::containers`. C owns its atomic context; C++ uses opaque pointers.
One producer publishes complete values and one consumer borrows the freshest
published value. Application code connects message callbacks to this mailbox.

## Storage and initialization

The mailbox never allocates.  The application owns its opaque context and
three fixed-size value slots:

```c
struct control_setpoint slots[WL_LATEST_SLOT_COUNT];
wl_latest_t mailbox;
wl_latest_config_t config = {
    .value_size = sizeof(slots[0]),
    .value_alignment = _Alignof(struct control_setpoint),
    .initial_generation = 0,
};
wl_latest_requirements_t requirements;

wl_latest_requirements(&config, &requirements);
/* sizeof(slots) must cover requirements.storage_size. */
wl_latest_init(&mailbox, &config,
               &(wl_latest_storage_t){slots, sizeof(slots)});
```

`wl_latest_requirements()` validates the size/alignment pair and returns the
rounded slot stride, total byte count, and fixed slot count.  The base storage
address must satisfy `value_alignment`.  All long-lived storage is therefore
known before initialization.

The context uses C11 32-bit atomics.  `wl_latest_init()` checks every atomic
object with `atomic_is_lock_free()` and returns `WL_ERR_NOT_SUPPORTED` rather
than silently introducing a library lock on an unsuitable target.

## Ownership and lifetime

The producer calls `wl_latest_write_claim()`, writes the entire fixed-size
value in place, and then calls `wl_latest_write_publish()` or
`wl_latest_write_abort()`.  Only the producer may access the writable pointer,
and it becomes invalid at publish/abort.

The consumer calls `wl_latest_read_acquire()`.  It returns the newest value
not previously acquired, or `WL_ERR_NO_DATA`.  The returned pointer is stable
through the matching `wl_latest_read_release()`: the producer may publish and
coalesce newer values but can never reclaim the borrowed slot.  The consumer
must stop accessing the pointer before release returns.  A role may have only
one active claim/view.

Initialization and reset require external quiescence.  Apart from those
operations the API is exactly SPSC: using multiple producers or consumers is
outside the C memory model contract, even if calls happen not to overlap.

## Retain owned values in a generated callback

Configure each mailbox slot for a generated owned `*_value_t`. In an ordinary
message callback, claim a slot, copy with `*_value_from_view`, check the result
and publish; abort if conversion fails. The consumer then acquires/releases an
owned value whose string/bytes/nested storage is independent of RX. See the
[build-backed copy example](tutorial-rpc-values.md).

Generated routes use their own decode storage; application code does not replace
route scratch or release kernel RX. Copying a borrowed view only retains RX
pointers and cannot extend their lifetime. Finish deep copying before callback
return. Mailbox claim/publish remains an application-producer operation, separate
from network delivery and idempotency.

## Why non-atomic values are race-free

The three slots have exclusive `front`, atomic `middle`, and exclusive `back`
ownership.  The producer writes only `back`, then uses a release exchange to
place it in `middle`; the returned former middle becomes its new back.  The
consumer uses an acquire exchange to move a dirty middle to `front` and place
its old front in middle.  The acquire/release chain transfers ownership before
either side touches a recycled slot.

Consequently the payload itself can be an arbitrary non-atomic C structure.
This is not a seqlock: no reader copies memory while a writer might mutate it.

## Freshness and counters

Publishing over an unread dirty middle slot increments `coalesced`; values
already acquired by the consumer are never counted as coalesced.  A successful
publish receives the prior generation plus one modulo 2^32.
`initial_generation` allows a retained generation to continue after restart
and makes wrap behavior testable.  Zero is a valid generation after wrap, and
ordering across wrap uses ordinary modular counter rules.

`publishes`, `reads`, `coalesced`, `empty_reads`, `resets`, and `errors`
saturate at `UINT32_MAX`.  `errors` counts invalid lifecycle operations after
a successful initialization; an ordinary empty read has its own counter and
is not an error.  A stats read is safe during SPSC operation, but its
independently loaded fields are an observational snapshot rather than one
transaction.  A second claim/acquire while the same role still owns a token
returns `WL_ERR_BUSY`; a stale or mismatched finish token returns
`WL_ERR_INVALID_STATE`.

`wl_latest_reset()` discards any unread value, makes the mailbox empty, and
increments `resets`.  It deliberately preserves generation and all cumulative
counters.  Reset with an outstanding writer claim or reader view fails.
