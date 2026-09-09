# Reliable RPC / telemetry coexistence

This is a deterministic **protocol-time** regression, not a CPU benchmark or a
prediction of UART, USB, H7 or OS scheduling latency. It runs real generated
ABI 29 endpoints, owned async RPCs, retained telemetry and the core with a fake
clock and a serialized asynchronous transport. No sockets, threads, sleeps or
Google Benchmark dependency are required. [中文结果与设计](../../docs/mixed-traffic-progress-cn.md).

A separate [H7 app](../zephyr/mixed_traffic/README-cn.md) reuses this workload and
adds real DWT measurements, including cached versus reconstructed retries.
See the [H7 results and measurement limits](../../docs/mixed-traffic-h7-cn.md);
running the model on a board does not turn its ages into USB/UART latency.

## Run

Build a matching development WLC independently, then point CMake at it:

```sh
cmake -S benchmarks/mixed_traffic -B build/mixed-traffic \
  -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc/target/release/wlc
cmake --build build/mixed-traffic --parallel 2
ctest --test-dir build/mixed-traffic --output-on-failure
build/mixed-traffic/wirelink_mixed_traffic --report > after.jsonl
python3 benchmarks/mixed_traffic/compare.py before.jsonl after.jsonl
```

`--report` still requires complete, correct RPC results, exactly one business
execution per call, exercised fault injection and immutable in-flight bytes.
`--check` additionally bounds telemetry age/gap by the scenario's physical
backpressure, sampling interval and serialization allowance. This gate fails
against the pre-change core; it is not a percentage timing threshold.

To compare another core without replacing the working tree, configure a separate
build with `-DWIRELINK_SOURCE_DIR=/path/to/exported/wirelink`. The harness/schema
come from this directory; the linked core comes from the selected source tree.
Use the same WLC and harness for both. Record the source revision/diff and binary
hashes; `compare.py` validates all 42 JSONL records before printing maxima.
Do not overwrite a previously frozen baseline binary with candidate sources.

## Workload and metrics

Each case runs 2,400 one-millisecond owner ticks, default 16-event endpoint
passes, and all three frame envelopes with CRC32C. Both directions emit samples
every 5 ms. RPC cases start five calls per direction at ticks 100/500/900/1300/1700,
with a 1,500 ms call budget, 100 ms ACK timeout and four retries.

| Case | Fault/load |
| --- | --- |
| `telemetry_only` | No RPC; one-millisecond physical I/O |
| `rpc_clean` | RPC plus telemetry, without faults |
| `single_ack` | Drop only the first ACK in each direction |
| `drop_ack` | Drop the first two ACK copies of every reliable sequence |
| `backpressure` | Return BUSY during a 20 ms window every 250 ms |
| `drop_ack_backpressure` | Combine recurring ACK loss and BUSY windows |
| `saturated_wrap` | Combined faults, 1 ms sampling, 3 ms I/O, telemetry attempted before step, wrapping clock |

The application keeps one latest unsent sample, replacing it on each sampling
tick. Once the core accepts a BUSY submission, that frame is already owned by
the transport path and is not replaced; finite backpressure remains visible.

- `max_age_ms`: age of the receiver's last application value on **every tick**,
  including ticks with no new packet. It does not reset when a mailbox is read.
- `max_arrival_age_ms`: age only when a new value arrives. Alone this misses
  long outages followed by a fresh packet.
- `max_gap_ms`: largest interval between observed updates. The age metric also
  covers an outage that extends to the end of the observation window.
- `mean_age_ms`: sum of observed value ages divided by all 2,400 ticks; the
  initial ticks before the first reception contribute zero.
- `max_rpc_ms` / `rpc_completed`: worst call completion time and total correct
  calls across both directions. These are repeated in each receiver record,
  not two separate sets of calls to add together.

Permanent BUSY, an owner that stops running, overloaded RX consumption and a
real medium with different serialization/wake behavior are outside this model.
Fair access does not mean telemetry cannot consume bandwidth or delay an RPC.

## Zephyr

The same workload is a Ztest integration app in
`tests/zephyr/integration/mixed_traffic`. From an initialized Zephyr workspace:

```sh
west twister -T /path/to/wirelink/tests/zephyr/integration/mixed_traffic \
  -p native_sim -p qemu_cortex_m3 -p qemu_riscv32 -j 2 \
  --extra-args=WIRELINK_WLC_EXECUTABLE=/path/to/wlc/target/release/wlc
```

Isolate unrelated product modules as required by the workspace. These runs
validate target ABI/toolchain behavior using simulated protocol time; their
wall time and printed ages are not firmware CPU measurements.
