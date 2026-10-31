# Kernel RPC and application telemetry model

[中文](README-cn.md).

This deterministic 2400-tick model runs real generated kernels and packet ports
in both directions. It is a protocol/application-time regression, not a CPU,
USB/UART latency or OS scheduling benchmark. The unchanged Echo/Telemetry
business schema now has an explicit v2 native packet profile. One shared kernel
runs over native packets, an explicit COBS/CRC16 connector or length16/NONE.
There is no endpoint selector, ACK engine or generated retention service.

```sh
cmake -S benchmarks/mixed_traffic -B build/mixed \
  -DCMAKE_BUILD_TYPE=Release -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc-0.13
cmake --build build/mixed --parallel
ctest --test-dir build/mixed --output-on-failure
build/mixed/wirelink_mixed_traffic --report > after.jsonl
python3 benchmarks/mixed_traffic/compare_kernel.py before.jsonl after.jsonl
```

The seven cases are telemetry-only, clean RPC, one dropped request, one dropped
response, finite BUSY windows, combined loss/BUSY and saturated telemetry with
a wrapping owner clock. Each RPC direction runs five application jobs. A job
explicitly retries a timed-out call, at most three attempts, with a fresh kernel
operation. A dropped response therefore executes the business handler twice.
The model asserts exactly those extra executions; it does not provide or assume
a replay cache, rollback or exactly-once execution. Real non-idempotent business
operations need their own durable job/idempotency IDs.

The application owns one latest unsent telemetry value and replaces it on each
sampling tick. Kernel admission snapshots accepted data before return; a BUSY
physical frame is never replaced in place. The controlled driver retains the
actual source until CONSUMED, keeps an owned transfer copy until peer admission
or deliberate loss, then publishes DRAINED. BUSY has a finite resource wake.
Close actually abandons remaining model transfers before reporting TX_ERROR;
it does not manufacture a successful physical drain. There are no sockets,
threads, sleeps or heap allocation in the model.

Format 2 contains all 42 scenario/connector/direction rows. The current validator
rejects missing/duplicate rows, invalid counts, missing fault injection and a
false once-only claim under response loss. Ages/gaps cover the receiver's last
application value, including quiet tails; `max_arrival_age_ms` covers arrivals
only. RPC duration includes application retries. `--check` also applies the
explicit scenario serialization/backpressure bound. Record identical workload
sources, toolchains, flags and binary hashes before comparing model results.

`compare.py`, `compare_firmware.py` and their tests remain **historical report
tools** for the old ARQ experiment. They do not parse current captures. Removed
cached/reconstructed retry execution is listed in
`../retired-execution-workloads.csv`. Frozen reports are unchanged.

The Zephyr integration suite runs this same model on simulators. Its firmware
wrapper emits `mixed_kernel_fw_*_v1` records and one cycle group per case, with
a 2400-tick IRQ-masked intrinsic diagnostic. Those cycles include the model's
copying, loss inspection and application bookkeeping; they are not end-to-end
active CPU and cannot be compared with old retry-engine groups. No board flash
is needed for model correctness tests; the dedicated static-kernel USB workload
supplies current RPC/Sample hardware evidence.

Current firmware captures use `compare_kernel_firmware.py before.log after.log`. It requires all 42 traffic rows and 21 cycle groups, validates actual duplicate-execution counters and rejects resets, missing/end records, mixed old formats or changed clock/layout context. Simulated milliseconds and IRQ-masked model cycles remain distinct; no physical throughput or historical ARQ CPU comparison is implied.
