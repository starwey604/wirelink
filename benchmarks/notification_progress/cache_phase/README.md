# Read cache counters without adding per-call hooks

This research tool modifies independent source snapshots, never the product or
SDK working tree. It adds private Control commands and whole-phase IBUS counter
reads at START/STOP. The Sample measurement and owner-loop source are unchanged.

```sh
python -B benchmarks/notification_progress/cache_phase/prepare.py \
  --baseline /absolute/A-off/sources --out /absolute/new-inputs \
  --host-main /absolute/registered-gated-main.cpp
```

Build the copied benchmark's `usb/static_kernel/device` for the existing
ESP32-S3 target and matching WLC, observer off/on separately. Build each host
with `UB_HOST_MAIN=/absolute/new-inputs/host-main.cpp`. The device app includes
the private `probe.c`; portable core, codecs, adapters and original observers
are not edited. Preparation refuses an existing destination or a non-unique
source anchor and emits `probe.patch`.

## Compare one image with counter reads off and on

Use the **same host binary within each observer mode** for all three groups:

- **A**, retained product image: `WL_CACHE_PROBE=-1`, skip private commands.
- **Q**, diagnostic image: `WL_CACHE_PROBE=0`, no phase counter reads.
- **P**, exactly the same diagnostic image as Q: `WL_CACHE_PROBE=1`, read two
  counters at each phase boundary. No per-call probes in either mode.

The existing identity override must identify the actual device, not the host's
compiled identity. Control `0x7f20` configures the mode before warmup; `0x7f21`
exports one frozen page after CPU/stat collection, outside measured windows.
Register identities, tool versions, work, ordering, worker scheduling, graphical
and power state before collecting. The 2026-11-01 execution first stopped an
A/Q/P block on a graphical transition, then retained a failed restart refused
before warmup on inherited-priority drift. Its final independent same-image
Q/P block uses `/home/ww/codings/wirelink-cache-phase-20261101/paired-stable/tools/`.
It reuses the parent's six unchanged business qualifications, not their timing
as new measurements. All attempts remain in their original directories.

**Q/A measures structural perturbation**: app code and link layout, startup
counter-filter setup, and outside-window private traffic differ. It does not
isolate layout alone. **P/Q measures incremental boundary reads within one
image**, including any resulting phase/timing changes. A large Q/A effect blocks
extrapolation to A even when P/Q is small. Do not search padding, function order,
cache settings or forced inlining to make a probe pass.

### Wait for an existing launcher policy instead of renicing

On the measured host, Ananicy assigns `bash` the Doc-View nice−4 rule. A Python
child forked before that asynchronous rule takes effect can instead inherit
nice16 from the tool host. This was reproduced in one shell without locking.
Freeze the actual native workers as usual. If the registered environment expects
nice−4, launch from a shell that has already acquired that **existing** policy:

```sh
bash benchmarks/notification_progress/cache_phase/wait_parent.sh -4 \
  /path/to/python -B /absolute/registered/controller.py
```

The helper observes its own priority for up to five seconds, keeps the shell
alive and forks only after it matches. It never calls renice, changes scheduler
policy, stops a service or changes system rules; a mismatch fails before the
controller starts. Do not use −4 as a universal benchmark default. Read the
registered environment and current system rule first, and keep the per-worker
pre-warmup and running checks. A successful flash with a refused pre-warmup smoke
still needs a health check in the actual environment; it is not a firmware fault.

## What the hardware counts cover

The ESP32-S3 SDK defines `EXTMEM_IBUS_ACS_CNT_REG` as IBUS accesses through
ICache and `EXTMEM_IBUS_ACS_MISS_CNT_REG` as misses caused by IBUS accesses to
external memory. The probe sets only the counter's address filter to
`SOC_IROM_LOW..SOC_IROM_HIGH-1`. It does not clear counters, flush cache, change
cache geometry, enable overflow interrupts or move code to IRAM. Restore the
product firmware after research.

Counts are unsigned modulo-32 deltas, read sequentially rather than atomically
as a pair. Phase state has one application-owner writer; hardware events cover
all contexts, including IRQs. START begins after the existing CPU snapshot;
STOP ends at handler entry. Counts include boundary control work. Stream counts
also include preparation/drain, whereas stream CPU/goodput retain the original
five-second window. Do not divide those different windows into one cost, call
IBUS accesses retired instructions, or treat phase misses as API-exclusive.
Require positive accesses and a nonzero RPC miss count as a hardware sanity
check; an unsupported counter must not become a reported zero-miss result.

## Replay before removing unused images

```sh
python -B -m unittest discover \
  -s benchmarks/notification_progress/cache_phase -p 'test_*.py'
python -B benchmarks/notification_progress/cache_phase/analyze.py \
  --evidence /absolute/wirelink-cache-phase-20261101/paired-stable \
  --out /absolute/new-replay.json
```

Replay checks all twelve registered Q/P captures, private mode/filter
acknowledgments, phase pages, original business/byte ledgers, image/host identity,
raw file integrity, every native worker's scheduling, graphical state, and the
original per-call distribution. Graphical state is frozen per pair, not assumed
constant for the whole desktop session. A crossing pair is retained and excluded;
there is no replacement pair. Comparisons are stratified by state, and fewer
than three eligible same-state pairs remain undecidable. Keep the evidence
parent's qualification captures alongside the paired block. Replay needs those
raw records and frozen metadata, not deleted experiment binaries. Same-image
observations remain diagnostic when structural or stability guards fail; they
are not new product scores.
