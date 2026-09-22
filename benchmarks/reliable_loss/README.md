# Reliable-path packet-loss benchmark

[中文](README-cn.md).

This benchmark measures how the reliable link path behaves when the transport
drops datagrams: how long a transaction takes to converge, how many attempts it
costs, and when retries are exhausted. It isolates the retry, ACK and duplicate
logic from a real socket, so a run is deterministic and reproducible.

## Model

Two Wirelink links exchange one reliable transaction at a time through a seeded
lossy channel. The links are real: they run `wl_send_reliable`, the fixed-ACK
retry timer, automatic ACKs, duplicate suppression and retry exhaustion. Only
the channel is synthetic.

- Each unit a link emits is dropped with probability `p`, otherwise delivered
  after a fixed one-way `delay_ms`.
- Both directions drop independently with the same probability, so DATA and its
  ACK can each be lost.
- A simulated millisecond clock advances one step per iteration. Retries fire on
  the link's own `ack_timeout_ms`; convergence time is therefore a protocol time,
  not a wall-clock sample.
- The RNG is seeded, so the same arguments always produce the same report.

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
build/reliable-loss/benchmarks/reliable_loss/wirelink_reliable_loss \
  --samples 2000 --loss 0,1,5,10,25,50
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
| `--loss LIST` | - | Comma-separated loss percents |
| `--json PATH` | - | Write a JSON report instead of CSV |

## Output

One CSV row per loss case (`wirelink_reliable_loss_v1`) or a JSON report with a
`cases` array. Fields:

| Field | Meaning |
| --- | --- |
| `completed` / `failed` | Measured transactions that succeeded / exhausted retries |
| `mean_ms`, `p50_ms`, `p95_ms`, `p99_ms`, `max_ms` | Convergence time over completed transactions |
| `attempts_per_success` | DATA transmissions per completed transaction (`1 + retries`) |
| `data_drops` / `ack_drops` | Units the client-to-server / server-to-client channel dropped |
| `duplicate_rx` | DATA frames the receiver already had (mostly ACK loss) |
| `goodput_bytes_per_s` | Payload bytes per simulated second |

Because the clock is simulated, `*_ms` values are protocol times: with
`--delay-ms 1` and no loss a transaction converges in exactly 2 ms. `goodput`
is a relative, not a bitrate, figure.

## Compare

```sh
python benchmarks/reliable_loss/compare.py before.json after.json
python benchmarks/reliable_loss/compare.py before.json after.json \
  --max-regression-percent 5
```

The comparison requires the same configuration and loss set, then reports each
metric per loss case. Latency, attempt and failure growth counts as a
regression, as does a goodput or completion drop. Because the benchmark is
deterministic, a change beyond export/import noise is a real change in the
reliable path.

## Interpretation

`duplicate_rx` tracks `ack_drops`: when an ACK is lost the sender retransmits,
and the receiver recognizes the retransmission as a duplicate and re-ACKs it.
That re-delivery is the cost of ACK loss and is visible here instead of being
inferred. `failed` appears once `max_retries` is too small for the loss rate, so
raise it when sweeping high loss.

## What it does not measure

- Real sockets, the OS scheduler, kernel queue drops and adapter batching. Those
  belong to the real-transport benchmarks; this one isolates the protocol.
- More than one outstanding reliable transaction: the link reserves one reliable
  transaction until it is taken.
- Duplicate or reordered injection, correlated (burst) loss, or congestion
  control. The channel drops independently and in order.
- Wall-clock CPU cost of the retry path; use the host CPU benchmarks for that.
- The application/RPC layer; this is the link-level reliable transaction.

## Procedure

Freeze a JSON report before editing the core, record the seed and configuration,
then rebuild and compare. Keep the same arguments on both sides; change the seed
to check that a result is not an artifact of one loss pattern.
