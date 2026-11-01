# Distinguish Sample window exceptions from stalls

This ESP32-S3 research probe prepares independent copies of the retained A
sources. It does not modify the portable core, SDK, product benchmark or
accepted firmware. It is a mechanism experiment, not a product optimization.

```sh
python -B benchmarks/notification_progress/pmu_sample/prepare.py \
  --baseline /absolute/A-off/sources --out /absolute/new-inputs \
  --host-main /absolute/registered-gated-main.cpp
```

Build the copied benchmark's `usb/static_kernel/device` for
`esp32s3_devkitc/esp32s3/procpu`, with matching WLC and observer off/on separately.
Build each corresponding host with `UB_HOST_MAIN=/absolute/new-inputs/host-main.cpp`.
Preparation refuses an existing destination or a changed source anchor and
emits the effective `probe.patch`. Calibration assembly belongs only to the
copied platform application, in ordinary `.text`.

## Keep structural and sampling effects separate

Use the same host within each observer mode, with `WL_PMU_PROBE` set to:

| Group | Value | Device and work |
| --- | ---: | --- |
| A | −1 | Original product image; skip private commands |
| Q | 0 | Diagnostic image; phase counters and Sample attempt ledger only |
| P | 1 | **Same image as Q**; additionally sample 1/64 Sample calls |

Control `0x7f20` configures the mode before warmup and returns calibration.
The host refuses an uncalibrated device. Control `0x7f21` exports frozen pages
after the original CPU/stat collection, outside measured business windows.
Qualification checks business and byte ledgers in all six groups. Q/A reveals
structural perturbation, not layout alone; P/Q measures incremental sampling.
Even unselected P calls run in a phase containing selected calls.

The fixed schedule selects index `i` when
`i % 64 == ((i // 64)*17+17) % 64`: 16 of 1000 successful calls, eight of each
parity, one in every 64-call stratum. It visits all 64 offsets over 4096 calls.
All original 1000 elapsed times remain exported, including the cold call.
Attempts, busy responses and errors are counted after the original elapsed
clock. A retry at a selected successful index is sampled again but only the
successful attempt is stored; the complete retry count remains in the ledger.

## Calibrate the actual hardware before USB starts

The SDK defines two PMU counters on this core. The probe claims them only when
the global enable register is zero. It uses explicit selector/mask fields:

- PM0: EXR selector 5, WINDOW mask `0x20`.
- PM1: I_STALL selector 4, ALL mask `0x1ff`.
- TRACELEVEL 15, KRNLCNT 0, overflow interrupt disabled.

The ALL stall mask includes instruction-related **and other global stalls**;
it is not a cache-only counter. The definitions are in the SDK's
`xt_perf_consts.h` and `xtensa-debug-module.h`; see also the
[Espressif perfmon API](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/perfmon.html).

Each boot checks PMU cycles against CCOUNT, 32 shallow versus 32 deep CALL8
chains, division stalls, control readback, and 32 empty snapshot brackets.
Calibration briefly locks interrupts before USB starts. Business measurement
does not mask interrupts. Counters then free-run; deltas use modulo-32 unsigned
arithmetic. No counter clears during business, cache flush, cache setting,
IRAM migration, forced inline, padding or function-order search is used.
The IBUS filter covers `SOC_IROM_LOW..SOC_IROM_HIGH-1` as in the phase probe.

## Read the intervals correctly

Each sparse row contains 11 unsigned words:

```text
index, original_clock_start, original_elapsed,
inner_window_delta, inner_stall_delta, IBUS_miss_delta,
WINDOWBASE_before, WINDOWSTART_before, WINDOWBASE_after, WINDOWSTART_after,
outer_window_delta
```

Inner snapshots fall inside the original two CCOUNT calls and exclude those
helpers. The outer window bracket includes both helpers. Snapshot macros add
no CALL8/CALLX8 frames; inspect actual disassembly to confirm only the original
two clock calls and `send_message` remain in that bracket. Instructions,
register allocation and layout still change. Selected elapsed times include
snapshot work. Empty-bracket maxima are calibration observations, **not costs
to subtract** from individual calls.

Reads are sequential, not atomic, and cover all contexts including IRQs.
The window, stall and IBUS-miss intervals have different endpoints. Do not
add/subtract them as independent exclusive costs. Equal window counts can
reject an event-count explanation in this diagnostic image; they do not prove
equal exception-handler latency. A miss count has no PC attribution and may
include the probe itself. Phase totals additionally include boundary traffic;
stream totals include preparation/drain.

## Register, replay, then remove experiment binaries

Use the existing hardware collector's identity, USB, scheduling and business
guards. Freeze each pair's graphical/power state and retain/exclude an entire
crossing pair without replacement. Never reuse runtime environment or samples
across a host reboot. If the serial device disappears, keep that attempt
incomplete and explicitly verify restoration of A when it returns.

The final 2026-11-01 block is
`/home/ww/codings/wirelink-pmu-window-20261101/r3/`; its frozen `tools/controller.py`
records three QP/PQ/QP pairs per observer. It reused unchanged r2 binaries,
but performed six fresh qualifications and registered a new environment.
The earlier interrupted r2 block remains separate.

```sh
python -B -m unittest discover \
  -s benchmarks/notification_progress/pmu_sample -p 'test_*.py'
python -B benchmarks/notification_progress/pmu_sample/analyze.py \
  --evidence /absolute/wirelink-pmu-window-20261101/r3 \
  --out /absolute/new-replay.json
```

Replay validates raw integrity, calibration, pages, original Sample clocks,
attempt ledgers, identities, workers and state eligibility. It reports all 1000
and the same deterministic 984 unselected calls in Q/P, plus event groups with
their elapsed/stall ranges. Event grouping across eligible pairs is descriptive;
performance comparisons remain state-stratified. It requires raw records and
metadata, not build directories. Restore A and pass its smoke before deleting
unused diagnostic device/host build trees permanently; retain source, raw data,
configuration and machine-code evidence, not compiled payload archives.
