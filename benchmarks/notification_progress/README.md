# Notification and progress census

This is an optional, non-timing research model of the accepted static kernel.
It uses the real generated profile-v2 USB schema, both RPC roles, C-owned port
storage, RX claims/releases and physical TX tokens. Each case performs 32 RPCs
with caller roles alternating, plus 64 ordinary messages. Admission inputs are
poisoned after copying; delayed source leases and exactly-once callbacks are
checked. It does not modify the product core, adapter or runtime.

```sh
cmake -S . -B /absolute/new-build -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_NOTIFICATION_CENSUS=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/matching-wlc
cmake --build /absolute/new-build --target wirelink_notification_census
/absolute/new-build/benchmarks/notification_progress/wirelink_notification_census
```

Eight JSON rows cover budgets 1/16 and inline SENT, async DRAINED, split
CONSUMED/DRAINED and a model of normal Zephyr USB class callbacks. The last
case includes RX rearm activity and separate final outer wakes after the
callback's final adapter access. It does not run the USBD stack or model every
fault/stop path. Port notify calls and direct outer wakes are separate counters.

`latch_already_set` and `signalled_not_ready_steps` describe an explicit software
binary-latch scheduler. They are neither OS wakeups nor real device owner pass
counts. There are no clock/cycle/timing fields. Do not turn notification counts
into a predicted percentage speedup or remove a lifecycle wake based on them.

`accepted_cpu.py` reads selected B-current raw CPU observations, checking each
input's membership/hash against the accepted evidence index. It supports
retention of raw records after obsolete build assets are deliberately removed;
it explicitly does not claim a full historical build-artifact replay. Inclusive
thread/IRQ observations must not be added/subtracted as exclusive costs.

The research reference is `baseline/static-kernel-accepted-20261031`, with actual
dirty inputs identified by `docs/notification-progress-baseline-20261031.json`.
Subsequent hardware work compared full-path counters and then a
[five-field owner probe](../../docs/static-endpoint-owner-local-result-20261102-cn.md).
The latter preserved the target regression directions without increased H2D
outer passes/retries. Prioritize the actual H2D receive/decode execution cost
before changing notification semantics; diagnostic and product evidence remain
separate.

## Leave the last tested firmware installed

The user's handoff instruction from 2026-11-02 is **no automatic reflash of the
original image when an experiment finishes**. Close owned USB/UART processes
and handles, record the last attempted and last healthy firmware identities,
and leave the last healthy tested image installed. This does not promote a
diagnostic image to the accepted scientific baseline. Retain the existing A
recovery assets; permanently delete obsolete build caches/images without
archiving them. Historical controllers that restored A in `finally` must not be
reused unchanged for new experiments.
