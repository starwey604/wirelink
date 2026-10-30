# Pre-release benchmark checklist

[中文](RELEASE-cn.md).

Choose two committed revisions with the same product and workload contract.
The 0.13 cut cannot be compared to a pre-cut release by rebuilding both with
current presets. Use its sealed historical tools and explicitly report the
semantic difference. No benchmark run authorizes publishing or pushing.

1. Run correctness/package/compiler checks first. Set matching local
   `WIRELINK_WLC_EXECUTABLE` (0.13) and
   `WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR` (1.9+).
2. Run `python3 benchmarks/bench.py doctor --preset full`. Use one identified,
   quiet host, identical compiler/flags, power policy and affinity. Timing
   during builds or correctness tests is not a qualified performance run.
3. Choose a compatible committed base and candidate; preserve the complete
   output directories. The seven suites are covered once by smoke, quick and
   the api suite:

```sh
python3 benchmarks/bench.py pair --preset smoke --base BASE --candidate CANDIDATE --out out/release-smoke
python3 benchmarks/bench.py pair --preset quick --base BASE --candidate CANDIDATE --out out/release-quick
python3 benchmarks/bench.py pair --suite api --preset full --base BASE --candidate CANDIDATE --out out/release-api
```

4. Inspect raw, manifest, normalized reports, comparisons, Markdown and JUnit.
   Exit 0 means valid/enabled gates pass, 1 a valid regression, 2 invalid or
   incomparable evidence. Investigate a smoke failure or exit 2. Timing gates
   stay diagnostic until calibrated on the identified testbed. Model results
   are simulated time/counts, footprint objects are not linked-product RAM,
   and API pair results include real RAM delivery and owner work.
5. For affected physical paths, use the matching firmware/HIL procedure and
   preserve binaries, sources, configuration, captures, observer settings,
   failed attempts and final board state. Hardware measurements stay separate
   from host microbenchmarks. Re-evaluate compatibility when a workload changes.

Ordinary pushes/PRs do not run performance measurements. The manual benchmark
workflow uses these current presets; remote CI, hardware runs and release
publication require their own recorded execution, never an inferred pass.
