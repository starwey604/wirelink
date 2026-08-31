# FIFO and command-flow performance procedure

This benchmark measures the allocation-free SPSC FIFO independently from a
Wirelink transport.  It is a host microbenchmark, not a wire-latency claim.
Its purpose is to catch large regressions in FIFO ownership handoff and to
quantify the queueing cost of the cross-thread command pattern described in
[`fifo.md`](fifo.md).

Build an optimized image without the platform adapters:

```sh
cmake -S . -B build/fifo-bench -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_FIFO_BENCHMARK=ON \
  -DWIRELINK_BUILD_EXAMPLES=OFF
cmake --build build/fifo-bench --target wirelink_fifo_benchmark
build/fifo-bench/benchmarks/fifo/wirelink_fifo_benchmark \
  --handoffs 1000000 --commands 250000 --capacity 64
```

The executable reports three separate costs:

- `handoff` is a claim, encode-like direct slot fill, publish, acquire,
  validation, and release on one thread. `ops_per_second` counts complete
  records, not individual API calls.
- `stats` repeatedly takes the documented non-transactional statistics
  snapshot. FIFO publish/consume counters are always enabled, so this is the
  incremental cost of querying them, not a comparison against a special
  statistics-disabled build.
- `command_flow` uses one pthread as the session consumer and the main thread
  as the client. Commands move through one FIFO and results through a second
  FIFO with the SPSC roles reversed. Every eighth command retains its borrowed
  head across one simulated `BUSY` retry. `enqueue_to_submit_ns` measures from
  publication timestamp to the successful consumer-side submit boundary.

Run at least five times on an otherwise idle machine. Record CPU model,
compiler, optimization flags, capacity, and iteration counts. Report the
median run for throughput and `ns_per_op`, while retaining the worst observed
p99. CPU affinity and a fixed performance governor improve reproducibility,
but are environmental controls rather than benchmark requirements.

The benchmark has no fixed absolute pass threshold because scheduler and CPU
frequency dominate host-to-host comparisons. For the same machine and build
profile, investigate a handoff or command-flow throughput regression above
10%, a statistics snapshot regression above 20%, or an enqueue-to-submit p99
increase above 20%. Correctness remains a hard gate: every run validates exact
ordering and exits unsuccessfully on a torn, duplicate, missing, or corrupted
record.

## Initial host record — 2026-08-31

The first retained run used GCC 16.2.1 on Linux x86-64 and an Intel Core 5 315
(six cores), with a Release build, 48-byte records, capacity 64, 1,000,000
handoffs, and 250,000 commands. CPU affinity and a fixed governor were not
applied; frequency scaling was active, so these numbers establish the output
contract and order of magnitude rather than a release threshold.

| Measurement | Median of five runs |
| --- | ---: |
| Complete single-thread handoffs/s | 41.25 million |
| Complete handoff time | 24.24 ns/record |
| Statistics snapshots/s | 261.54 million |
| Statistics snapshot time | 3.82 ns/call |
| Bidirectional command results/s | 2.11 million |
| Bidirectional command time | 472.93 ns/record |
| Enqueue-to-submit p50 | 28.594 us |
| Enqueue-to-submit p99 | 43.698 us |

The worst p99 among the five scheduler-sensitive command runs was 128.853 us;
the largest individual sample was 1.118 ms. Every run completed all records
with exact validation. The variation is why comparisons must retain the same
CPU controls and use repeated runs rather than treating this workstation
snapshot as a portable performance promise.
