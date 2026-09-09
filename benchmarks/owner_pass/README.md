# Executor owner-pass accounting

This is a deterministic scheduling regression, not a CPU benchmark. It requires
neither WLC, Asio, Google Benchmark nor hardware:

```sh
cmake -S . -B build/owner-pass -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_HOST_RUNTIME=ON -DWIRELINK_HOST_ACTIVITY=ON \
  -DWIRELINK_BUILD_OWNER_PASS_TESTS=ON
cmake --build build/owner-pass --parallel 2
ctest --test-dir build/owner-pass --output-on-failure
build/owner-pass/benchmarks/owner_pass/wirelink_owner_pass
```

The harness gates the first owner pass to publish an already-ready burst. This
test-only gate is **not** a timed batching policy in the executor. Tests validate
payloads, delivery counts, bounded dispatch, RX/RPC fairness, application-requested
follow-ups, asynchronous TX, backpressure sleep/resume, empty feeds, overflow
recovery and stop during continuous replenishment. A proxy completion exercises
the real caller/owner handoff; generated RPC/UDP integration is tested separately.
The compatibility cases omit the adapter readiness hint deliberately.

Each scenario emits one JSON row after joining the owner. The separate overflow
check uses assertions. Startup/shutdown notifications count too. Thread start can
race with the first pass; do not use exact idle-pass counts as a portable gate.

`WIRELINK_HOST_ACTIVITY` defaults OFF. It enables bounded relaxed atomic counters,
not timestamps or hot-path logging. `Executor::activity()` returns disabled/zero
when OFF; diagnostics must never control application synchronization. The PUBLIC
definition is exported with `Wirelink::host`; rebuild all consumers together.

Important distinctions:

- `passes` is outer executor iterations, not endpoint steps or OS context switches.
- `no_reported_work` means no pump-reported progress, LATEST progress or collected
  RPC jobs. It can include a necessary idle/readiness check or unreported adapter
  work; it does **not** establish wasted CPU by itself.
- `rpc_batches` counts nonempty collections; `rpc_empty_collections` counts stale
  pending hints that yielded no jobs. `rpc_completions` includes failed/cancelled
  results as well as success.
- `latest_attempts` includes deferred/rejected attempts. `latest_sent` means core
  acceptance, which precedes physical delivery for asynchronous adapters.
- `notifications` counts calls, not semaphore releases or actual thread switches.
  `waits` counts wait attempts, which can consume an already-present notification.
- `continue_*` identifies the branch that requested another pass; reasons need
  not be mutually exclusive in reality. Snapshots during execution are relaxed,
  not a coherent multi-counter transaction; compare final joined snapshots.

For CPU and submission-to-sink age, use the separate
[executor benchmark](../api/EXECUTOR-cn.md) with activity/timing instrumentation OFF.
See the [measurement and tradeoffs](../../docs/owner-pass-performance-cn.md).
