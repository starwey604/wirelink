# Pre-release benchmark checklist

[中文](RELEASE-cn.md).

Run this checklist on the release commit **before** creating the release tag.
The comparison answers whether the candidate regressed from the previous
release; it is not a correctness test or a performance guarantee.

## Policy

- Ordinary pushes and pull requests do not run performance measurements.
- The base is the previous release tag and the candidate is the clean release
  commit.
- Base and candidate run on the same machine, toolchain, power policy, and
  attached board. Do not compare results from different testbeds.
- A smoke regression or exit `2` must be investigated before tagging. Host
  timing and API/UDP results remain diagnostic until a stable dedicated
  testbed has calibrated thresholds.
- Preserve the complete output directories, not just the Markdown summary.

## Preferred local run

Use a quiet, stable host for the authoritative result. Configure the pinned
WLC and Google Benchmark source first:

```sh
export WIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc
export WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/absolute/path/to/google-benchmark

python3 benchmarks/bench.py doctor --preset full
```

Verify that the candidate is committed and choose the previous release tag:

```sh
git status --short
base=v0.8.0
candidate=$(git rev-parse HEAD)
release_out=out/release-candidate
```

The three comparisons below cover all eight registered host suites without
running any suite twice:

```sh
python3 benchmarks/bench.py pair --preset smoke --order ab \
  --base "$base" --candidate "$candidate" --out "$release_out/smoke"

python3 benchmarks/bench.py pair --preset quick \
  --base "$base" --candidate "$candidate" --out "$release_out/quick"

python3 benchmarks/bench.py pair --preset full --suite api \
  --base "$base" --candidate "$candidate" --out "$release_out/api"
```

Use a new `release_out` for every attempt; evidence directories are exclusive
and are never overwritten.

## Optional GitHub run

The `Benchmarks` workflow is manual-only. Dispatch it on the release commit
with profile `release`. Set `base` to the previous tag; when blank, the
workflow selects the newest older tag reachable from the candidate. The
workflow runs smoke, quick, and API in one job and retains all evidence for 30
days.

GitHub-hosted timing is useful for a diagnostic second opinion, but it does
not replace the stable local or self-hosted testbed result.

## Review and archive

Before creating the tag:

1. Confirm every comparison is valid; exit `2` means rerun or fix the testbed.
2. Investigate each smoke regression. Fix it or record why the change is an
   intentional product tradeoff.
3. Review quick timing rows that exceed 10% and 5 ns together, plus API memory
   and UDP distributions. These are advisory on non-dedicated hosts.
4. Archive `manifest.json`, `raw/`, `artifacts/`, `comparison.json`,
   `summary.md`, and `junit.xml` for all three comparisons.
5. Record the base tag, candidate commit, testbed ID, and disposition of every
   accepted regression in the release notes or release tracking issue.
6. Create the release tag only after this review is complete.

Hardware benchmark/HIL automation is not unified yet. When a release requires
firmware measurements, follow the relevant `benchmarks/zephyr/` suite guide
and archive its ELF, map, config, raw serial log, and board identity alongside
the host evidence.
