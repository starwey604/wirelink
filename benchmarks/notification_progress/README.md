# Deterministic notification and ownership census

This optional, non-timing model exercises the accepted static kernel with the
real generated profile-v2 USB schema, both RPC roles, C-owned port storage,
RX claims/releases and physical TX tokens. Each case performs 32 alternating
RPCs and 64 ordinary messages. It checks exactly-once effects/callbacks, poisoned
admission inputs, delayed source leases and quiescent close.

```sh
cmake -S . -B /absolute/new-build -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_NOTIFICATION_CENSUS=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/matching-wlc
cmake --build /absolute/new-build --target wirelink_notification_census
ctest --test-dir /absolute/new-build -R '^notification_census$' --output-on-failure
/absolute/new-build/benchmarks/notification_progress/wirelink_notification_census
```

Eight JSON rows cover budgets 1/16 and inline SENT, asynchronous DRAINED,
split CONSUMED/DRAINED and a normal USB callback model. The latter includes RX
rearm publication and an outer wake after the callback's final adapter access.
It does not run USBD or model all fault/stop interleavings. Port notify calls
and direct outer wakes are separate counters.

`latch_already_set` and `signalled_not_ready_steps` describe the model's software
binary latch, not OS wakeups or actual device owner passes. There are no timing
fields; notification counts do not predict CPU savings or justify removing a
lifecycle wake.

## CPU investigation closed

Temporary probes, candidate builders/controllers and one-off analysis tools have
been removed. The [convergence record](../../docs/static-endpoint-cpu-convergence-20261111-cn.md)
identifies the retained regression tests, rejected candidates and historical
evidence. Those experiments did not establish a new accepted performance
baseline or the final cache/bus/scheduling cause.

For later authorized hardware work, do not automatically restore an original
image on completion: record the last healthy firmware and leave it installed.
Permanently delete unused compiled payloads/caches without archiving them.
The historical controllers in frozen evidence are not supported execution
entry points and must not be reused as-is.
