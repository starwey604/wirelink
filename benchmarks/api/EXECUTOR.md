# Executor multi-producer performance regression

This benchmark isolates thread-to-thread submission, wakeups, result
notification and LATEST replacement. The ordinary [API benchmark](README.md)
still covers codec/endpoint CPU and UDP RTT; it does not replace this
contention measurement.

## Build and run

Reuse the API benchmark's WLC, Asio and Google Benchmark arguments, adding
`-DWIRELINK_BUILD_EXECUTOR_BENCHMARK=ON`. It enables the host runtime and
additionally generates a 12-service/8-slot endpoint; it does not change the
existing 1/4-slot baseline.

Configure two Release build directories:

- OFF: `-DWIRELINK_HOST_PROFILING=OFF -DWIRELINK_HOST_LOCK_PROFILING=OFF
  -DWIRELINK_HOST_ACTIVITY=OFF` for real CPU and latency.
- LOCKS: `-DWIRELINK_HOST_PROFILING=OFF -DWIRELINK_HOST_LOCK_PROFILING=ON` for
  locating locks and wakeups.

```sh
cmake --build build/executor-off --target wirelink_executor_benchmark --parallel 2
ctest --test-dir build/executor-off -R '^executor_benchmark_' --output-on-failure
python benchmarks/api/executor_run.py \
  --binary build/executor-off/benchmarks/api/wirelink_executor_benchmark \
  --profile off --out executor-off.json
python benchmarks/api/executor_run.py \
  --binary build/executor-locks/benchmarks/api/wirelink_executor_benchmark \
  --profile locks --out executor-locks.json
python benchmarks/api/executor_analyze.py executor-off.json > executor-off-summary.json
python benchmarks/api/executor_analyze.py executor-locks.json > executor-locks-summary.json
```

The script repeats each case three times by default with a fixed randomized
interleave and keeps each raw log and binary SHA-256. Each producer sends 5000
saturated calls and 1000 paced calls; `--skip-paced` skips the paced group.
Reports are never overwritten and no timing gate is set automatically on a
shared machine. Output from failed processes is kept beside the logs.

Summaries keep min/median/max over repetitions; "median p99" is not a global
p99 over all calls. Stage `mean_ns` is the average per lock-acquisition or
wakeup batch, while `ns_per_call` amortizes over business submissions.

## Workload meaning

| Mode | What actually runs |
| --- | --- |
| `rpc` | Business threads call the generated `_sync`; one executor owner advances two real loopback endpoints |
| `proxy` | The real executor's queue/wakeup/CV completion; submit completes inline in the owner, without protocol |
| `latest` | Each producer uses its own message ID; real outbox, frame encoding and owner sink |
| `shared` | All producers share one message ID, exercising replacement when only the latest is kept |

The matrix is 1/2/4/8 producers, 32/512 B, and either no sleep or a 1000 µs
interval **per producer**. Eight paced producers are roughly 8 kHz combined,
not 1 kHz combined.

Run one case directly as `MODE PRODUCERS CALLS_EACH BYTES PERIOD_US`:

```sh
build/executor-off/benchmarks/api/wirelink_executor_benchmark rpc 8 5000 32 0
```

RPC validates every response; LATEST validates the full payload, per-producer
ordering and the final value after going idle. LATEST's original
`p50_ns`/`p99_ns` are **submit return times**, not delivery times; input
throughput is not send throughput, so read `dispatched` and `coalesced`
together. A shared channel may have its final value replaced by another
producer, so a separate marker after producers exit checks final drain.
`coalesced` counts replacements of an outbox entry that is still valid,
possibly including entries the owner has copied but not completed; it is not an
exact dropped-frame count and cannot be added to `dispatched` to check total
submissions. The shared marker counts toward both counts, but not toward
business calls.

`sink_samples`, `sink_p50_ns` and `sink_p99_ns` are also emitted: they record
the submission time in the payload and measure the age of a surviving update
when the owner sink validates or accepts it. This includes queueing, encoding
and the benchmark's own sink decode, but not the network or remote reception,
and it is not a continuous age-of-information or update interval; updates
replaced by LATEST produce no delivery sample. The benchmark reserves the
sample array before measurement, and both versions must use the same
instrumentation. Payloads are now at least 16 B.

Process CPU includes the owner and business threads; wall time includes
barriers, scheduling, result validation and final drain. Initialization happens
before measurement; this benchmark excludes socket, USB, CAN or motor work.
`proxy` is not an RPC network benchmark. On Windows, process CPU uses
`GetProcessTimes`, not `std::clock`, which returns wall time. Take formal
measurements on an idle host; cross-platform source does not imply Windows or
macOS performance acceptance.

## Instrumentation and limits

`latest_{submit,take,finish}_{wait,hold}` and
`rpc_{admit,collect,finish}_{wait,hold}` record wall time acquiring and holding
the mutex. Summaries happen after unlock so diagnostic atomics stay out of the
critical section. `notify_to_owner` approximates the first coalesced
notification to the next owner pass, not per-call queue time.

A lock stage with `cpu_ns=0` means **not measured**, not zero CPU cost; wait
wall time includes preemption. Condition-variable waits release the mutex, so a
whole RPC wait is not lock hold time. Completion notification still happens
under the mutex to protect the caller's stack job/CV lifetime.

LOCKS mode turns off the broad Scope CPU clock, but diagnostic summarization
still costs and can perturb thread interleaving. OFF data is for final
CPU/latency judgement; LOCKS is only for locating. Do not treat ON/OFF
differences as "lock-removal savings". Stage ranges can nest or overlap across
threads and cannot be summed into whole-machine utilization. Full
`WIRELINK_HOST_PROFILING=ON` remains available for other stage attribution; use
`--profile full` with the run script.

## Owner pass and paired regression

Configure a separate build with `-DWIRELINK_HOST_ACTIVITY=ON` and both timing
options off to inspect the JSON `activity`. `passes`, `rpc_jobs`, `rpc_batches`
and `latest_attempts/latest_deferred` distinguish repeated advancement, queue
collection and backpressure probing. `endpoint_steps` records the benchmark's
endpoint calls separately: the current RPC driver calls endpoint step four
times per outer pass, and those four are not repeated executor scheduling.
Counting rules and an independent correctness case are in the
[owner-pass harness](../owner_pass/README.md).

`executor_analyze.py` keeps reading old reports; new reports additionally
summarize delivery age, counts, passes per submission and per actual delivery,
and RPC calls per nonempty batch. It rejects mixed switches, budgets or
incomplete counts. A zero in OFF is not zero cost.

Freeze two OFF binaries built from the same benchmark code, then run them
serially and alternately:

```sh
python benchmarks/api/executor_pair.py --before /path/to/baseline \
  --after /path/to/candidate --out build/executor-paired
```

Defaults are 1/8 producers, 32/512 B, saturated and 1 kHz per-producer, four
modes and three repetitions; each case randomly picks which version runs first,
for 192 processes total. Saturated per-producer calls default to 3000 and paced
to 300, adjustable by the same-named arguments. The output directory must not
exist; failed logs, binary digests, both raw reports and min/median/max
summaries are kept. Do not run this alongside compilation, CTest, Zephyr or
other stress. For saturated LATEST, also compare actual delivery counts:
discarding more updates is not a CPU optimization.
