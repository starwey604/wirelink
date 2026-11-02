# Count owner passes and send retries on matched diagnostic A/B

Use this stage to distinguish repeated owner work from increased cost per
execution after the required-fixed encoder candidate. It counts device phase
totals, not individual-call clocks, and keeps the original host binary/workload.

```sh
python -B benchmarks/notification_progress/owner_census/prepare.py \
  --frozen /absolute/wirelink-required-fixed-20261101/inputs \
  --out /absolute/new-diagnostic-inputs
```

The preparer copies the frozen sources and applies strict, exact-anchor patches.
Build this same source tree with the accepted A WLC and experimental B WLC,
separately for observer off/on. It reuses `hardware/diagnostic.c` with 24 event
names: lock-free relaxed uint32 additions and single-core IRQ-locked snapshots.
There are no SDK edits, semaphore internals probes, new host probes or consuming
reads of product state. The live core/WLC worktrees are not the diagnostic input.

## Count complete phases; keep window CPU separate

The counter window begins after START's CPU snapshot and stops at entry to the
STOP handler. It includes stream preparation and drain. START/STOP occur inside
kernel steps, so aggregate budget-work counts can straddle the boundaries.
`step_empty` means the existing budget count was zero; a counted backpressured
RX attempt is not an empty step. `service_active` means the existing RX-rearm
condition was true, not that a frame necessarily progressed.

Counters cover owner passes, two adapter services per pass, kernel steps/empty
steps/budget work, RX release, physical TX submit/busy/publication/drained/take,
wakes, wait branches/timeouts, and Chunk/Progress admission attempts/results.
They do not infer actual semaphore-block duration or exclusive framework cost.

After STOP has closed its CPU window and formatted its unchanged reply, three
counter pages are printed to UART. They are not added to wire replies. Printing
and counter instrumentation perturb layout/scheduling; diagnostic throughput
and timing are not product A/B results. Do not divide the original 5 s window
CPU by these full-phase counters.

## Replay raw data after deleting all diagnostic builds

```sh
python -B benchmarks/notification_progress/owner_census/analyze.py \
  --evidence /absolute/wirelink-owner-census-20261102 \
  --out /absolute/new-census-replay.json
python -B -m unittest discover \
  -s benchmarks/notification_progress/owner_census -p 'test_*.py'
```

The execution root freezes the collector/controller, source audit, firmware and
shared-host identities, fresh qualifications and AB/BA/AB order per observer.
That 24-counter execution restored original A-on in `finally`; subsequent runs
follow the parent directory's no-automatic-reflash handoff policy. Scheduling
timelines and the host-state
sidecar apply to every arm; transition/gap-crossing pairs are excluded whole,
without replacement. Comparisons also separate different power states.

Replay checks raw business/byte ledgers, all UART pages, field/domain/result
partitions, owner:step:service accounting and successful Chunk counts. For each
screen it cross-checks admitted Progress against successful wire Progress
recovered with the shared `a_residency/stream_ledger.py` byte model. That model's
old `inspect_stream.py` entry points remain available.

Three same-state pairs and each series span <=5% are needed for stable count
inference; zero references remain explicit. Report whether diagnostics preserve
the original stream gap before interpreting counts. If probes erase/reverse the
gap, those counts describe diagnostic builds, not the original regression.
The original product candidate remains unpromoted. Keep source/raw/text evidence
and the current A recovery assets; delete new compiled payloads permanently.

The [execution result](../../../docs/static-endpoint-owner-census-result-20261102-cn.md)
records a non-representative diagnostic contrast. A separately registered,
single exact-image product control is under `control/`; replay includes it when
registered, verifies original image identities and recovery, and never pools it
with either matrix. Restoring the old HAL compile-time log literal using
`SOURCE_DATE_EPOCH` reproduced B's original BIN identity before this control.
All five new firmware build trees and the Rust cache were subsequently deleted.

## Use five owner-local fields before adding more probes

```sh
python -B benchmarks/notification_progress/owner_census/prepare_local.py \
  --frozen /absolute/wirelink-required-fixed-20261101/inputs \
  --out /absolute/new-local-inputs
python -B benchmarks/notification_progress/owner_census/analyze_local.py \
  --evidence /absolute/wirelink-owner-local-20261102 \
  --out /absolute/new-local-replay.json
```

This variant changes only the benchmark backend/main and adds a private header.
Five plain uint32 fields count owner passes, Chunk attempts/WOULD_BLOCK and
Progress attempts/WOULD_BLOCK. One owner writes them and takes the snapshots;
there are no per-event helper calls or atomics. Shared core, port, adapter, SDK
and host are unchanged. Check actual Xtensa call lists, frame sizes and counter
loads/adds/stores before flashing; smaller source is not a timing guarantee.

The CPU-on execution predeclares an exact-product AB control, then a local AB
pilot. Both must stay in the same graphical/power state and both H2D and
bidirectional D2H B/A must remain below0.95. Otherwise stop, without trying more
configurations or replacing pairs. A passing pilot permits the remaining BA/AB
count pairs. This is a direction-preservation gate, not equality of all timing
behavior or product acceptance. The recorded run left the final healthy local B
installed, with no automatic restoration.

Snapshots span START/STOP including preparation/drain. Successful admissions
are derived as attempts minus WOULD_BLOCK, checked against business/USB ledgers
under the existing zero-error requirement. Do not divide 5 s window CPU by
these whole-phase counts. Device Progress in single-direction D2H is the phase
END marker: keep its counts/ratios, but do not interpret its per-frame density
as hot-path amplification. Replay labels this boundary scope explicitly.

The [five-field result](../../../docs/static-endpoint-owner-local-result-20261102-cn.md)
contains the three same-state series, the limits of the gate and installed
firmware identity. Existing 24-counter replay/entry points retain their original
meaning and historical recovery checks.
