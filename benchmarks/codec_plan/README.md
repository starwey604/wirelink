# Compile-time codec planning

Standalone CPU workload for field lookup and fixed packed-array specialization.
No Google Benchmark dependency or diagnostic code enters the Wirelink core.
The same C fixture runs on a host and an H7 with an independent encoded-byte oracle.

## Host comparison

Freeze the old compiler and generate its artifacts **before** changing WLC.
Generate the new artifacts separately; both iterations in this experiment use
ABI 29 because public interfaces/layouts and wire formats do not change.

```sh
/path/to/old-wlc compile benchmarks/codec_plan/plan.wl --out-dir build/codec-old
/path/to/new-wlc compile benchmarks/codec_plan/plan.wl --out-dir build/codec-new
cmake -S benchmarks/codec_plan -B build/codec-bench-old -DCMAKE_BUILD_TYPE=Release \
  -DCODEC_PLAN_CODEC_DIR="$PWD/build/codec-old" \
  -DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/path/to/google-benchmark
cmake --build build/codec-bench-old --parallel
ctest --test-dir build/codec-bench-old --output-on-failure
build/codec-bench-old/wirelink_codec_plan_benchmark \
  --benchmark_min_time=0.03s --benchmark_repetitions=7 \
  --benchmark_out=before.json --benchmark_out_format=json
```

Repeat in a separate build directory using `codec-new`, saving `after.json`.
Google Benchmark 1.9+ can also be supplied through `find_package(benchmark)`.
Use identical compilers/flags, an idle host and A/B/B/A run order; do not compile
or run other tests concurrently with timing. Compare with:

```sh
python3 benchmarks/codec_plan/compare.py before.json after.json
python3 benchmarks/codec_plan/test_compare.py
```

Preserve compiler, codec, executable and raw-report hashes. The parser rejects
partial matrices, duplicate repetitions, errors, changed encoded lengths and
machine/workload mismatches. Compiler/flags need separate provenance verification.
Correctness smoke is not a timing gate; no fixed threshold is imposed on shared CI.

### Keep correctness and timing separate

CTest's short benchmark runs only check correctness; their printed timings are
not regression measurements. Finish all builds before formal timing. Run one
benchmark process at a time, with no CTest, Twister, compiler, sanitizer or other
benchmark running alongside it. On hybrid/mixed-frequency hosts, pin **both**
frozen executables to the same suitable CPU, for example `taskset -c 1` on Linux
after checking `lscpu -e=CPU,CORE,MAXMHZ`. Record the affinity; do not compare a
pinned run against an unpinned run or mix the two datasets. Frequency scaling and
other system work can still introduce noise even with affinity.

Use A/B/B/A ordering with repeated samples, then run correctness CTest suites and
Zephyr Twister separately after timing completes. H7 RTT collection also belongs
in a separate phase when validating host performance. Keep exploratory, smoke
and formal logs distinguishable rather than treating every printed duration as
performance evidence.

## Matrix

| Kind (`k`) | Shape |
| --- | --- |
| 0–4 | 2/8/16/32/64 uint32 fields, contiguous IDs |
| 5–8 | 8/16/32/64 uint32 fields, sparse IDs (stride 997) |
| 9 | Optional packed float32[30], field ID 1 |
| 10 | Required packed float32[128], field ID 21 |
| 11 | Optional packed float64[64], field ID 65535 |

Patterns (`p`): 0 all fields in canonical order, 1 reversed scalar field order,
2 last scalar field only with leading/trailing unknown fields. Packed arrays
remain present in all patterns; pattern 1 intentionally has the same single-field
content as pattern 0. Stages (`s`): 0 public view decode, 1 public view encode,
2 public encoded-size calculation. Total: **108 host groups**.

These are codec entry costs, not full RPC latency or owned conversion cost. The
existing [API benchmark](../api/README.md) separately measures RPC and telemetry
through actual endpoint/link processing.

## H7 and instruction cache

Build `benchmarks/zephyr/codec_plan` from an initialized Zephyr workspace, selecting
the board, board root, module list and frozen `CODEC_PLAN_CODEC_DIR`. Both sides
must use the same configuration. The local board is `dm_mc02` / STM32H723ZG.

Primary configuration enables LTO, speed optimization, I/D cache and local ISR
table declarations. CMake **fails** if Kconfig did not actually enable LTO. Verify
`CONFIG_LTO=y` in `.config` and `-flto` in the actual build commands as well.
For the diagnostic no-LTO pair, pass
`-DEXTRA_CONF_FILE=/absolute/path/to/benchmarks/zephyr/codec_plan/no_lto.conf`
and `-DCODEC_PLAN_EXPECT_LTO=OFF`. Do not compare different LTO settings as an A/B.

The H7 matrix has three modes (`m`):

- 0: warmed homogeneous messages, 32 operations per sample.
- 1: invalidate I-cache before each timed operation, 32 operations per sample.
- 2: rotate all twelve message shapes, 36 operations per sample (three each).

Each group has five samples: **225 groups / 1,125 samples per boot**. DWT reads
bracket each operation, with compiler memory barriers to prevent LTO hoisting
work across timing. IRQ masking covers only the test operation; cache invalidation,
validation, logging and sleeping are outside measured cycles. The timer/call
overhead is retained identically on both sides, not estimated and subtracted.

Use an active J-Link Commander RTT server and the existing
`benchmarks/zephyr/willow_cpu/capture.py`. Start capture before running the reset
core, and never open a second probe. `compare.py before.log after.log` requires
complete boot-to-pass records, the timing-barrier marker and equal LTO/frequency.

Mode 1 is an artificial cold-entry stress test, **not an I-cache miss counter**;
mode 2 is a controlled mixed workload, not proof of every product working set.
Report hot, cold and mixed cases separately alongside total linked FLASH/RAM.
This app is not Willow; its free FLASH does not establish product headroom.

Implementation, measured results and remaining scope:
[中文性能记录](../../docs/codec-plan-performance-cn.md).

The unchanged v1 matrix is also used for the
[cursor/key-precomputation follow-up](../../docs/codec-convergence-performance-cn.md).
For that comparison, freeze the already-optimized compiler as the new baseline;
do not relabel the original pre-planning baseline or overwrite its artifacts.

The [fallback follow-up](../../docs/codec-fallback-performance-cn.md) also checks
repeatability of each unchanged executable. On Linux, a separate diagnostic run
can use `setarch x86_64 -R taskset -c 1 <benchmark> ...` to disable ASLR for that
process only. Record `/proc/self/personality` in the same launch environment;
do not change global ASLR/governor settings or combine these samples with ordinary
ASLR-enabled runs. This is a controlled microbenchmark, not proof of all application
code/data layouts. Full-matrix completeness alone does not establish repeatability.
