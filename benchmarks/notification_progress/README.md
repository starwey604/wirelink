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
Next hardware work needs distinct publication/notify/wait/step counters and a
separate observer-perturbation check before optimizing any notification path.
