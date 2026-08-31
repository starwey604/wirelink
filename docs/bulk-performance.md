# Sequential bulk runtime performance procedure

The host bulk benchmark measures Wirelink's allocation-free sequential sender
and receiver state machines independently from a transport. It is a CPU and
state-machine regression benchmark, not a USB, UDP, flash, or end-to-end
latency claim. Physical transports must retain their own hardware benchmarks.

Build an optimized image without platform adapters:

```sh
cmake -S . -B build/bulk-bench -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_BULK_BENCHMARK=ON \
  -DWIRELINK_BUILD_EXAMPLES=OFF
cmake --build build/bulk-bench --target wirelink_bulk_benchmark
build/bulk-bench/benchmarks/bulk/wirelink_bulk_benchmark \
  --bytes 4194304 --iterations 8 --latency-iterations 2 --warmups 1
```

The default fixture owns a repeatable 4 MiB source and a same-sized sink. All
heap allocation occurs once during benchmark startup; neither bulk runtime
uses a heap. Each measured transfer executes the complete
`Begin -> Chunk x N -> End` action/Status flow and the receiver writes the
borrowed Chunk span directly to the caller-owned sink. The four chunk sizes
are 256, 512, 1024, and 2016 bytes. The final value conservatively reserves 32
bytes of the 2048-byte v1 payload ceiling for generated Chunk fields and
length prefixes; it is the benchmark's maximum-safe payload data span, not a
claim that every future schema has exactly 32 bytes of overhead.

The raw baseline performs the same chunk-sized `memcpy` calls into the same
sink followed by the same CRC32C calculation once per complete object. The
CRC result is consumed through a volatile checksum, and the executable also
checks the complete destination with `memcmp` after each pass. This prevents
the compiler from deleting either workload and avoids making the bulk runtime
look slower by charging it for object integrity while omitting integrity from
the baseline.

Reported fields have these meanings:

- `raw_goodput_mib_s` and `runtime_goodput_mib_s` use monotonic wall time and
  count application bytes, not control messages;
- `runtime_cpu_ns_per_byte` uses process CPU time and includes state changes,
  callbacks, copies, statistics, and final CRC32C;
- `goodput_ratio_pct` and `cpu_overhead_pct` compare the runtime with the
  matched raw sink baseline;
- `status_state_p50_ns` and `status_state_p99_ns` come from a separate,
  instrumented pass so per-action clock reads do not distort throughput. They
  cover synchronous local submit, receiver handling, Status acquisition,
  sender handling, and Status release. They exclude encoding, transport,
  scheduling, and hardware latency; and
- retry and BUSY counters must remain zero in this no-fault fixture.

Run at least five times on an otherwise idle machine. Record CPU, compiler,
optimization profile, options, and whether CPU affinity or a fixed governor
was used. Use the median run for throughput and CPU cost, retaining the worst
p99. The executable enforces correctness failures, a minimum 1 MiB object, and
at least two measured iterations.

For stable same-host Release comparisons, the initial regression goals are at
least 90% of matched raw goodput and no more than 10% additional CPU ns/byte.
The executable prints `met` or `missed` for both goals but does not turn an
environment-sensitive performance miss into a failed correctness run. A miss
must be recorded and investigated; it must never be rewritten as a pass.

The sequential stop-and-wait state machine performs one Status exchange per
Chunk. It therefore cannot promise 90% of raw-copy goodput at every very small
object/chunk combination. These goals primarily protect the 1024-byte and
maximum-safe paths; smaller chunks remain visible to quantify the fixed cost
that a later bounded window is intended to amortize.

## Initial host record — 2026-08-31

The initial record used an Intel Core 5 315 (six cores), Linux x86-64, CMake
Release builds, a 4 MiB object, eight measured iterations, two latency
iterations, and one warm-up. Each executable was pinned to CPU 2 with
`taskset`; frequency scaling remained enabled. GCC was 16.2.1 and Clang was
22.1.8. Rates, ratios, CPU cost, overhead, and p50 are medians of five runs;
the p99 column deliberately retains the worst p99 observed in those runs.

| Compiler | Chunk | Raw MiB/s | Runtime MiB/s | Goodput ratio | Runtime ns/byte | CPU overhead | Status p50 | Worst p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GCC | 256 | 347.75 | 337.47 | 98.40% | 2.7271 | 1.88% | 49 ns | 256 ns |
| GCC | 512 | 340.78 | 339.97 | 99.76% | 2.7086 | 0.70% | 60 ns | 286 ns |
| GCC | 1024 | 343.47 | 328.96 | 94.45% | 2.7860 | 4.34% | 267 ns | 691 ns |
| GCC | 2016 | 341.69 | 343.53 | 99.01% | 2.7374 | 1.40% | 450 ns | 1,101 ns |
| Clang | 256 | 345.83 | 332.30 | 97.57% | 2.7248 | 1.91% | 51 ns | 183 ns |
| Clang | 512 | 339.66 | 333.07 | 98.56% | 2.7093 | 1.67% | 64 ns | 333 ns |
| Clang | 1024 | 342.44 | 332.97 | 98.37% | 2.7637 | 2.76% | 251 ns | 637 ns |
| Clang | 2016 | 339.72 | 336.10 | 99.20% | 2.7293 | 1.28% | 407 ns | 2,006 ns |

All median comparisons met both initial regression goals, and every run had
zero retry, sender-BUSY, and receiver-BUSY counts with exact byte and CRC32C
validation. One scheduler/frequency-sensitive GCC 1024-byte run measured an
88.10% goodput ratio and 14.31% CPU overhead even though its median remained
inside the goals. That outlier is retained here to prevent interpreting the
goals as deterministic host promises without a fixed performance governor.

The roughly 2.7 ns/byte total is dominated by the table-driven whole-object
CRC32C, which is intentionally present in both measurements. Therefore the
small delta is a measurement of state-machine overhead around a realistic
integrity sink, not the isolated cost of the state machine divided by a
hardware-accelerated copy baseline.
