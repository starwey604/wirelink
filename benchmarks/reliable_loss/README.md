# Reliable-path packet-loss benchmark

[中文](README-cn.md).

This benchmark measures how the reliable link path behaves when the transport
drops, duplicates, reorders or bursts datagrams: how long a transaction takes to
converge, how many attempts it costs, and when retries are exhausted. It isolates
the retry, ACK and duplicate logic from a real socket, so a run is deterministic
and reproducible.

## Model

Two Wirelink links exchange one reliable transaction at a time through a seeded
fault channel. The links are real: they run `wl_send_reliable`, the fixed-ACK
retry timer, automatic ACKs, duplicate suppression and retry exhaustion. Only
the channel is synthetic.

- **Independent loss**: each emitted unit is dropped with probability
  `--loss-percent`, otherwise delivered after a fixed one-way `delay_ms`.
- **Duplicate**: a delivered unit is enqueued twice with probability
  `--duplicate-percent`.
- **Reorder**: a delivered unit is delayed by `--reorder-extra-ms` with
  probability `--reorder-percent`, so it arrives after the units emitted
  immediately after it.
- **Burst (correlated) loss**: a two-state good/bad channel. With probability
  `--burst-start-percent` per unit it enters the bad state, where units drop with
  `--burst-drop-percent` and the state lasts about `--burst-length` units.
- Both directions are affected independently with the same settings, so DATA and
  its ACK can each be lost, duplicated or reordered.
- A simulated millisecond clock advances one step per iteration. Retries fire on
  the link's own `ack_timeout_ms`; convergence time is therefore a protocol time,
  not a wall-clock sample.
- The RNG is seeded, so the same arguments always produce the same report. With
  all fault knobs at zero the channel is only independent loss.

This complements [`tests/tutorials/udp_processes.py`](../../tests/tutorials/udp_processes.py),
which exercises the same paths over real UDP for correctness, and
[`mixed_traffic`](../mixed_traffic/README.md), which measures application-level
ACK loss and telemetry age.

## Build and run

```sh
cmake -S . -B build/reliable-loss -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_RELIABLE_LOSS_BENCHMARK=ON -DWIRELINK_BUILD_EXAMPLES=OFF \
  -DWIRELINK_BUILD_PLATFORM=OFF
cmake --build build/reliable-loss --target wirelink_reliable_loss
```

```sh
# Independent loss sweep.
.../wirelink_reliable_loss --samples 2000 --loss 0,1,5,10,25,50

# Duplicate-only and reorder-only.
.../wirelink_reliable_loss --loss 0 --duplicate-percent 50
.../wirelink_reliable_loss --loss 0 --reorder-percent 50 --reorder-extra-ms 15

# Correlated bursts: 3% chance per unit to start, ~4 units long, total loss.
.../wirelink_reliable_loss --loss 0 --burst-start-percent 3 --burst-length 4 \
  --burst-drop-percent 100

# Everything at once.
.../wirelink_reliable_loss --samples 2000 --loss 2 --duplicate-percent 10 \
  --reorder-percent 10 --reorder-extra-ms 15 --burst-start-percent 1 \
  --burst-length 3 --burst-drop-percent 80 --json report.json
```

Options:

| Option | Default | Meaning |
| --- | --- | --- |
| `--samples N` | 2000 | Measured transactions per loss case |
| `--warmup N` | 32 | Unrecorded transactions before the window |
| `--payload N` | 64 | Reliable payload bytes |
| `--delay-ms N` | 1 | One-way channel delay |
| `--ack-timeout-ms N` | 20 | Link ACK timeout (fixed per retry) |
| `--max-retries N` | 20 | Retries before a transaction fails |
| `--seed N` | 0x574C524C | Channel RNG seed |
| `--loss LIST` | - | Comma-separated independent-loss percents |
| `--duplicate-percent P` | 0 | Per-unit chance to deliver twice |
| `--reorder-percent P` | 0 | Per-unit chance to arrive late |
| `--reorder-extra-ms N` | 10 | Extra delay for a reordered unit |
| `--burst-start-percent P` | 0 | Per-unit chance to enter the bad state |
| `--burst-length N` | 4 | Mean units spent in the bad state |
| `--burst-drop-percent P` | 0 | Drop chance while in the bad state |
| `--json PATH` | - | Write a JSON report instead of CSV |

The loss list is the swept variable; the other fault knobs are fixed for the
whole run. Combine invocations to sweep them separately.

## Output

One CSV row per loss case (`wirelink_reliable_loss_v1`) or a JSON report with a
`cases` array. Each row repeats the fault configuration, then the result:

| Field | Meaning |
| --- | --- |
| `completed` / `failed` | Measured transactions that succeeded / exhausted retries |
| `mean_ms`, `p50_ms`, `p95_ms`, `p99_ms`, `max_ms` | Convergence time over completed transactions |
| `attempts_per_success` | DATA transmissions per completed transaction (`1 + retries`) |
| `data_drops` / `ack_drops` | Units the client-to-server / server-to-client channel dropped |
| `duplicate_rx` | DATA frames the receiver already had |
| `duplicated_units` / `reordered_units` | Units the channel delivered twice / late |
| `goodput_bytes_per_s` | Payload bytes per simulated second |

Because the clock is simulated, `*_ms` values are protocol times: with
`--delay-ms 1` and no faults a transaction converges in exactly 2 ms. `goodput`
is a relative, not a bitrate, figure.

## Compare

```sh
python benchmarks/reliable_loss/compare.py before.json after.json
python benchmarks/reliable_loss/compare.py before.json after.json \
  --max-regression-percent 5
```

The comparison requires the same configuration (including the fault knobs) and
loss set, then reports each metric per loss case. Latency, attempt and failure
growth counts as a regression, as does a goodput or completion drop. Because the
benchmark is deterministic, a change beyond export/import noise is a real change
in the reliable path.

## Interpretation

- `duplicate_rx` counts both injected duplicates and retransmissions the receiver
  already had. Split them by comparing a duplicate-only run with a clean one.
- `reorder` mostly delays the ACK, so the sender times out and retransmits; the
  receiver then sees a duplicate. This shows up as `attempts_per_success > 1`
  with `reordered_units > 0`.
- `burst` is where the RTO policy is stressed: consecutive drops make several
  retries fail in a row, so `p95`/`p99`/`max` grow with `--burst-length`. Raise
  `--max-retries` before blaming failures on the protocol.
- `failed` appears once `max_retries` is too small for the fault level.

## What it does not measure

- Real sockets, the OS scheduler, kernel queue drops and adapter batching. Those
  belong to the real-transport benchmarks; this one isolates the protocol.
- More than one outstanding reliable transaction: the link reserves one reliable
  transaction until it is taken.
- A measured channel model. The burst state is a synthetic two-state process, not
  a fit to a real radio or link.
- Congestion control or rate adaptation; the channel does not depend on load.
- Wall-clock CPU cost of the retry path; use the host CPU benchmarks for that.
- The application/RPC layer; this is the link-level reliable transaction.

## Procedure

Freeze a JSON report before editing the core, record the seed and configuration,
then rebuild and compare. Keep the same arguments on both sides; change the seed
to check that a result is not an artifact of one loss pattern.
