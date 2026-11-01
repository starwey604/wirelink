# Count actual USB work before changing the wake path

The O1 tools instrument **independent source snapshots**, not the product or SDK
working tree. `prepare.py` consumes the source-only archives made by the current
`build-wirelink-usb-fs.py` builder and refuses an existing destination. Every
replacement requires an exact, unique source anchor; source drift fails instead
of silently missing a probe. The emitted patch and source manifest identify D.

```sh
python -B benchmarks/notification_progress/hardware/prepare.py \
  --baseline /absolute/A-off/sources --out /absolute/new-D-inputs \
  --host-main /absolute/registered-gated-main.cpp
```

Build the copied `wirelink-cross-benchmark/usb/static_kernel/device` with the
normal ESP32-S3 board, matching WLC, independent `UB_BUILD_ID`, observer off/on,
and the existing `cpu.conf` only for on. Build its host with
`UB_HOST_MAIN=/absolute/new-D-inputs/host-main.cpp`; use **separate off/on host
builds** so original Astrial/USB observer settings also match A. Qualification,
registration and capture must use distinct A/D identities and new output paths.
The 2026-11-01 execution's adapted capture/controller tools and source inputs are
in `/home/ww/codings/wirelink-cpu-optimization-20261101/`; no archived tool was
modified. Do not use D as a product performance candidate.

## What the counters mean

- Device counters are relaxed, lock-free atomic uint32 additions. All writers
  (owner, USBD callback and semaphore context) use the same atomic operations.
  Start/stop snapshots hold the local IRQ lock on the selected **single-core**
  ESP32-S3 configuration; this is not an SMP snapshot protocol. Deltas are modulo
  2^32; these bounded windows are well below one wrap per counter. There are no
  consuming reads/clears of product state, clock reads in core, hot-path prints or heap
  allocations. A has no diagnostic declarations, hooks or new product switches.
- Device windows begin after START's CPU snapshot and end at entry to STOP's
  handler. They include boundary control traffic. Because START/STOP execute
  inside a step, step entry and its final work count can straddle the boundary.
  Stream **counts include preparation and drain**; stream CPU/goodput retain the
  original 5-second measurement window. Do not divide window CPU by full-phase
  frame totals. `sample` counts inbound Sample callbacks; this workload sends
  Sample from the device, whose 1000-message ledger is exported separately.
- The semaphore probe is forced into `kernel/sem.c` at compile time. SDK files
  remain unchanged. `sem_immediate` reads count while the existing lock is held;
  `sem_block` is the actual branch before `z_pend_curr`, not a time threshold or
  speculative try-take. `sem_saturated` counts give calls when the token is
  already at its limit under that lock. Only the owner's registered semaphore
  is counted. Original take/give and scheduler operations remain intact.
- `step_work` is the existing budget count (including a backpressured receive
  attempt), while RX/TX/terminal/completion have distinct counters. Empty scans
  and generation snapshots count actual visited slots. The O1 server workload
  leaves all outbound calls free but does not remove either RPC role.
- Host counters are atomic uint64 additions around the actual CoalescingEvent
  notify/merge/consume/wait and USB adapter queries/service. Snapshots load each
  counter independently; boundary events can overlap. Host snapshots span START
  return through CPU-stat export, slightly wider than device snapshots.
- A failed recovery is still covered by the original health/error and console
  contracts. These captures exercise healthy traffic; they are not new hardware
  fault-injection, close-race or scheduler tests. No bounded event trace or
  exclusive function timing is produced by this first count pass.

## Replay raw records without restoring build caches

```sh
python -B benchmarks/notification_progress/hardware/analyze.py \
  --evidence /absolute/wirelink-cpu-optimization-20261101 \
  --out /absolute/new-o1-replay.json
python -B -m unittest discover \
  -s benchmarks/notification_progress/hardware -p 'test_*.py'
```

Replay checks the registered order, source tool identities, capture identity and
observer, file contents, scheduling timelines, complete business/byte ledgers,
all counter pages and accounting invariants. It recomputes metrics from raw
JSONL. A/D ratios measure **whole diagnostic-build perturbation**, including host
probes, code layout and observer interactions. They do not isolate instruction
cost or predict optimization gains. Deleted diagnostic images are intentionally
not needed for raw replay, and replay does not claim to rebuild/validate them.
