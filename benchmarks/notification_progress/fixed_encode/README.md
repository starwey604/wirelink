# Replay the required-fixed encoder candidate

This screen compares the retained A product firmware with one WLC code-generation
candidate, with no new firmware or host probes. The candidate preflights an
all-required, constant-width Schema-v1 scalar shape once, then writes fixed
byte offsets. Public codec entry points, wire bytes and preflight error behavior
remain covered by differential tests. Optional, varuint and other codec profiles
keep their existing generation path.

The execution record is
`/home/ww/codings/wirelink-required-fixed-20261101/`. Its `inputs/` contains frozen
A product/application/observer sources, not edits to the live benchmark or SDK.
Only the candidate WLC changes generated `usb.c` and its provenance manifest.
Within each observer mode A/B use the same retained gated host. Compiler and
firmware identities are recorded separately; no accepted A compiler is replaced.

## Require actual target work reduction before flashing

The frozen `tools/audit.py` uses the existing ELF reader (with the candidate's
explicit ELF artifact key), original A object-section receipt and actual B
machine code. For the successful Sample business path it found:

- 337 → 238 static instructions, excluding IRQ/window handlers and the clocks;
- 14 → 9 business calls, peak CALL8 depth after main 9 → 8;
- total `.text` −32 B in off/on, data/BSS and IRAM unchanged.

The encoder body itself grows from 158 to 186 B; whole-path work and linked text
are the relevant checks. Neither instruction counts nor inclusive elapsed clocks
are exclusive core costs or predictions of cycle/miss savings.

## Keep performance and mechanism claims separate

The frozen controller performs four fresh smoke qualifications, A/B boundary
checks and three AB/BA/AB pairs per observer. Each screen retains 1000 warmup,
10000 RPC, idle, low-frequency RPC, all 1000 paced Sample observations and 32B
streams in three directions. It records native-worker scheduling and a 1Hz
host-state sidecar; a graphical/power transition excludes an entire pair without
replacement. Comparisons never pool different graphical states.

Original 5% latency/CPU and 95% goodput protections remain, with a 0.5pp idle
allowance. Three same-state pairs and each series span ≤5% are required for a
stable comparison. Report p50/p99, all-call mean, histogram and cold call; do not
make the 50%-boundary p50 the sole improvement evidence. Registration requires
stable API mean improvement beyond 5% in at least one observer mode to claim a
measured API benefit. Repeated stable protection regressions reject the
candidate; reference instability stays undecidable. This is a 32B product
screen, not full O5 A/B/C acceptance or proof of the cache root cause.

```sh
python -B benchmarks/notification_progress/fixed_encode/analyze.py \
  --evidence /absolute/wirelink-required-fixed-20261101 \
  --out /absolute/new-replay.json
```

Replay uses frozen metadata, qualifications, raw host/UART data, worker and
state timelines. It checks final A-on smoke as well as the matrix. It does not
need deleted candidate build trees or compiler binaries. Keep the accepted A
recovery image; permanently delete unused candidate builds/caches after recovery
and replay, retaining source/raw/configuration/machine-code evidence only.

The [Chinese result](../../../docs/static-endpoint-required-fixed-result-20261101-cn.md)
records the decision: only two same-state pairs per observer survived, with
Sample gains and concerning stream losses. The candidate was not promoted. WLC
`main` keeps contract-test commit `df02154`; experimental source is on
`experiment/required-fixed-20261101` at `8ac90a9`. Original A-on was restored,
and all new compiled candidate/sample/Rust payloads were permanently removed.
