# USB transport performance procedure

This record compares three interrupt-driven ESP32-S3 Full-Speed USB paths with
`CONFIG_UDC_DWC2_DMA=n`:

1. `raw-bulk`: endpoint loopback, establishing the libusb + USB + UDC floor;
2. `wirelink-bulk`: custom Bulk with COBS, CRC32C, Wirelink polling, and echo;
3. `cdc`: CDC ACM IRQ ingress with the same Wirelink profile and echo.

All latency values are host-observed round-trip times. They are not mixed with
one-way device timestamps or the older RX-only benchmark.

## Standard matrix

- Board: ESP32-S3-N8R2, USB Full-Speed, same host port and cable for every run.
- Host: Release build, fixed performance CPU governor where available, no
  debugger logging during samples.
- Payload sizes: 8, 32, 64, 120, 128, 256, and 512 bytes. The 120-byte point
  represents six joints with five 32-bit MIT-control values per joint.
- Run: 1,000 warm-up exchanges followed by at least 10,000 recorded exchanges.
- Report: minimum, mean, p50, p95, p99, maximum, sequential payload goodput,
  timeouts, payload mismatches, adapter errors, and Wirelink RX counters.
- Repeat each cell three times after a fresh enumeration; retain the median run
  and note the range between runs.

The primary control-loop indicators are p99 RTT and maximum observed stall at
32 and 120 bytes. Sequential goodput is useful for regression detection but is
not a substitute for a saturated, windowed throughput test.

## Commands

Build the host tool with the local Astrial checkout:

```sh
cmake -S . -B build/usb-bench -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_HOST_BENCHMARKS=ON \
  -DWIRELINK_ASTRIAL_SOURCE_DIR=/home/ww/codings/astrial \
  -DASTRIAL_IO_URING=OFF
cmake --build build/usb-bench --parallel
```

Flash the matching sample before each command:

```sh
build/usb-bench/benchmarks/host/wirelink_transport_benchmark raw-bulk \
  --vid 0x2fe3 --pid 0x574c --payload 120 --warmup 1000 --iterations 10000

build/usb-bench/benchmarks/host/wirelink_transport_benchmark wirelink-bulk \
  --vid 0x2fe3 --pid 0x574c --payload 120 --warmup 1000 --iterations 10000

# Balanced host profile: sleep for both RX and TX completions.
build/usb-bench/benchmarks/host/wirelink_transport_benchmark wirelink-bulk \
  --idle wait --wake all --payload 120 --warmup 1000 --iterations 10000

# Request/response profile: coalesce TX completion into the RX wakeup.
build/usb-bench/benchmarks/host/wirelink_transport_benchmark wirelink-bulk \
  --idle wait --wake rx --payload 120 --warmup 1000 --iterations 10000

build/usb-bench/benchmarks/host/wirelink_transport_benchmark cdc \
  --port /dev/ttyACM1 --payload 120 --warmup 1000 --iterations 10000
```

The VID/PIDs in the samples are development identifiers only and must be
replaced before product distribution.

## ESP32-S3 results

### 2026-08-30 bring-up baseline

This is one formal run per cell after the transport fixes in `0825efa`, not yet
the three-run median required by the standard matrix above. Each cell contains
1,000 warm-up exchanges and 10,000 recorded sequential echo exchanges. No
timeout or payload mismatch occurred in the retained runs.

- Board: ESP32-S3-N8R2, revision v0.2, 240 MHz, 2 MiB PSRAM.
- Link: USB Full-Speed (12 Mbit/s), `CONFIG_UDC_DWC2_DMA=n`.
- Host: Intel Core 5 315, Linux 7.2.0-1-cachyos, `powersave` governor.
- Software: Zephyr `bd8c1538237`, libusb 1.0.30, Astrial `0c1a81e`, Release
  host build.
- Connection: the board's native USB device port; `/dev/ttyACM0` was the
  ESP32-S3 download/console port and CDC enumerated as `/dev/ttyACM1`.

#### Raw Bulk loopback

| Payload (B) | Min (us) | p50 (us) | p95 (us) | p99 (us) | Max (us) | Mean (us) | Payload B/s |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 72.76 | 88.72 | 144.00 | 172.88 | 949.82 | 96.05 | 83,290.52 |
| 32 | 102.42 | 129.38 | 176.20 | 202.12 | 1,471.51 | 132.64 | 241,250.75 |
| 64 | 224.34 | 246.34 | 304.23 | 335.55 | 912.63 | 254.94 | 251,038.75 |
| 120 | 323.62 | 415.43 | 481.35 | 511.82 | 1,273.56 | 421.41 | 284,755.85 |
| 128 | 421.21 | 502.11 | 560.93 | 604.45 | 1,917.52 | 507.44 | 252,244.60 |
| 256 | 925.73 | 1,001.39 | 1,145.44 | 1,207.41 | 2,639.89 | 1,028.19 | 248,980.31 |
| 512 | 1,921.47 | 2,266.99 | 2,318.04 | 2,380.68 | 3,437.18 | 2,238.84 | 228,689.45 |

The raw sample deliberately owns one 64-byte Full-Speed endpoint buffer and
reuses it alternately for OUT and IN. It is a useful small-packet floor, but it
serializes every USB packet for payloads above 64 bytes and is therefore not a
saturated Bulk throughput ceiling.

#### Wirelink custom Bulk

| Payload (B) | Min (us) | p50 (us) | p95 (us) | p99 (us) | Max (us) | Mean (us) | Payload B/s |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 61.01 | 193.97 | 221.96 | 269.66 | 1,423.33 | 198.45 | 40,311.92 |
| 32 | 64.92 | 232.92 | 301.40 | 344.21 | 640.06 | 246.64 | 129,745.20 |
| 64 | 157.15 | 335.58 | 396.09 | 449.40 | 1,290.76 | 333.87 | 191,690.26 |
| 120 | 210.49 | 485.81 | 571.75 | 624.34 | 1,380.46 | 489.83 | 244,983.18 |
| 128 | 357.64 | 501.81 | 571.75 | 619.66 | 944.43 | 502.45 | 254,752.32 |
| 256 | 532.75 | 788.24 | 871.07 | 920.19 | 1,761.00 | 788.43 | 324,697.23 |
| 512 | 1,110.75 | 1,433.26 | 1,542.52 | 1,597.88 | 1,835.51 | 1,435.67 | 356,627.68 |

The custom Bulk adapter receives into a 576-byte direct ring claim, large
enough for the configured maximum encoded transmission unit and aligned to the
64-byte Full-Speed packet size. This lets a complete large Wirelink frame enter
without the raw sample's per-packet OUT/IN alternation.

#### Wirelink CDC ACM IRQ

| Payload (B) | Min (us) | p50 (us) | p95 (us) | p99 (us) | Max (us) | Mean (us) | Payload B/s |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 253.27 | 362.30 | 442.97 | 480.08 | 1,638.92 | 365.62 | 21,880.52 |
| 32 | 289.36 | 346.65 | 405.35 | 481.41 | 1,666.26 | 354.78 | 90,196.88 |
| 64 | 521.12 | 658.23 | 735.71 | 770.61 | 1,967.57 | 657.59 | 97,325.16 |
| 120 | 697.50 | 878.90 | 956.66 | 991.70 | 1,507.87 | 874.55 | 137,213.22 |
| 128 | 706.49 | 838.98 | 907.56 | 935.57 | 2,148.11 | 834.99 | 153,295.35 |
| 256 | 1,157.98 | 1,362.25 | 1,450.50 | 1,491.03 | 2,658.58 | 1,366.29 | 187,369.23 |
| 512 | 2,174.64 | 2,358.67 | 2,486.74 | 2,548.69 | 3,674.63 | 2,364.35 | 216,550.38 |

At the 120-byte control payload, custom Bulk measured 485.81 us p50 and
624.34 us p99, versus CDC's 878.90 us p50 and 991.70 us p99. Custom Bulk
therefore reduced p50 by about 44.7%, p99 by about 37.0%, and increased
sequential payload goodput by about 78.5% in this run. The p99 result fits a
1 kHz round-trip budget, while the 1.38 ms maximum shows that a non-real-time
host still needs an application-level jitter margin.

### RX claim correctness found during the run

The first long run exposed two boundary bugs that short smoke tests did not:

- A direct ring claim at physical offset 4080 left only 16 bytes before the
  4096-byte ring end. Submitting that short tail to libusb caused a transfer
  overflow when the next full USB packet arrived. Both USB adapters now pause
  instead of submitting a claim shorter than `maximum_read_size`; after the
  consumer drains the ring, the next claim normalizes at offset zero.
- A 512-byte application payload expands beyond a 512-byte USB read after the
  Wirelink header, CRC32C, and COBS envelope. The benchmark and board sample now
  use the 576-byte configured transmission unit as their read size.

These constraints are part of the adapter contract: a direct USB read buffer
must not be an arbitrary short ring tail, and its configured read size must fit
the largest encoded transmission unit (with suitable endpoint-packet
alignment). The echo samples use a separate payload mailbox so they can release
the borrowed RX event before rearming OUT and retry safely if TX is busy.

### 2026-08-31 host API and CPU/latency iteration

This iteration used the same board, firmware, host port, cable, 120-byte
payload, 1,000 warm-ups, and 10,000 measured exchanges. The host tool was a
Release build using Astrial `4ec4dc3`. Shell `time` supplied user and system
CPU values; their sum is reported below. The host still used the non-real-time
`powersave` governor, so isolated maxima are recorded but not used to select a
strategy.

| RX path | Idle/wake policy | p50 (us) | p95 (us) | p99 (us) | Max (us) | Mean (us) | Payload B/s | CPU (s) | Wall (s) | Wakeups |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Direct ring | Poll | 453.74 | 522.62 | 563.78 | 768.64 | 459.12 | 261,368 | 5.271 | 5.186 | 0 |
| Direct ring | Wait, all completions | 473.01 | 539.94 | 572.58 | 3,101.97 | 478.48 | 250,792 | 0.768 | 5.400 | 21,989 |
| Direct ring | Wait, receive only | 476.02 | 545.28 | 596.51 | 5,225.99 | 484.76 | 247,546 | 0.704 | 5.502 | 11,002 |
| Four staged reads | Poll | 452.77 | 525.20 | 565.70 | 1,027.88 | 458.99 | 261,441 | 5.313 | 5.190 | 0 |
| Four staged reads | Wait, all completions | 473.55 | 541.08 | 586.35 | 1,307.09 | 478.42 | 250,825 | 0.776 | 5.401 | 21,994 |

The balanced default for an application that does not need continuous busy
polling is direct ring plus `wait`/`AllCompletions`. Relative to polling it
reduced measured host CPU time by 85.4%, while p50 increased 19.27 us (4.2%)
and p99 increased 8.80 us (1.6%). Polling remains the explicit lowest-latency
profile. The tested 50, 150, and 300 us yield/spin windows did not recover the
polling latency and consumed progressively more CPU, so no hybrid duration is
recommended.

`ReceiveOnly` is an opt-in request/response policy. It halved scheduler
wakeups and reduced CPU a further 8.3% in the retained Release run, but had a
24 us higher p99 than `AllCompletions`. The safe default therefore continues
to wake for both RX and TX. A receive-only user must wait with a finite
deadline so one-way TX and missing responses still reach `service()`.

The four-read staged prototype allocated four 576-byte adapter buffers and
copied every libusb completion into Wirelink's ring. It produced no meaningful
sequential RTT, goodput, or CPU improvement and worsened the wait-profile p99.
It is not retained. Astrial invokes its providers and completions on one event
thread, while Wirelink's destination ring is already SPSC, so placing an
external lock-free queue between them would add another ownership boundary and
buffering delay without removing a contended lock. The production adapter
therefore keeps one direct, zero-copy IN claim and no selectable RX backend.

Astrial's USB transfer hot path itself now dispatches RX callbacks directly
and gates its single borrowed TX with atomics. An interleaved before/after run
showed no latency shift outside board/host jitter, confirming that the removed
uncontended mutex was not the USB RTT bottleneck. The change is retained for a
simpler non-blocking transfer path and a race-free synchronous
`stop_reads()`, not claimed as a latency win.

The retained code then passed Astrial's three host tests, Wirelink's two host
tests, and the complete Zephyr unit/integration matrix: 21/21 configurations
and 105/105 cases across `unit_testing`, `native_sim`, `qemu_cortex_m3`,
`qemu_riscv32`, and `qemu_x86_64`.

### 2026-08-31 ESP32-S3 device CPU-cycle profile

The 120-byte polling profile was repeated three times on the same
ESP32-S3-N8R2 at 240 MHz. Each run used 1,000 warm-ups and 10,000 measured
sequential echo exchanges. The median host result was 484.39 us p50,
610.07 us p99, and 489.70 us mean RTT; all three runs completed without a
timeout, payload mismatch, or device adapter error.

This profile distinguishes the actual DWC2 interrupt from the deferred USB
work. The execution chain is DWC2 top-half ISR, DWC2 bottom-half thread,
Zephyr USBD event thread, then Wirelink's main-loop service. In particular,
the Wirelink class `request` callback runs in the USBD thread and is not an
ISR.

| Measured region | Calls/exchange | Mean/invocation (us) | Time/exchange (us) | Boot-wide max (us) | Cycle-window / elapsed |
| --- | ---: | ---: | ---: | ---: | ---: |
| DWC2 top-half ISR | 6.00 | 6.075 | 36.450 | 49.450 | 7.44% |
| DWC2 bottom-half event | 4.00 | 5.441 | 21.766 | 43.529 | 4.45% |
| Zephyr USBD event | 2.00 | 7.038 | 14.076 | 200.658 | 2.87% |
| Wirelink RX class callback | 1.00 | 5.851 | 5.851 | 38.958 | 1.20% |
| Wirelink TX class callback | 1.00 | 2.646 | 2.646 | 7.796 | 0.54% |
| Wirelink TX submit/sink | 1.00 | 24.719 | 24.719 | 74.088 | 5.04% |
| Active adapter service | 1.95 | 13.849 | 27.055 | 63.071 | 5.52% |

The approximately six top-half calls per exchange are consistent with a
120-byte Wirelink payload plus its envelope crossing several 64-byte
Full-Speed packets in each direction. During a continuous sequential load,
the measured DWC2 ISR therefore occupied about 7.4% of the single 240 MHz
core. Its mean cost was stable to 0.001 us across the three runs.

The table must not be summed into a total CPU percentage. The USBD row already
contains the RX and TX class callbacks, and cycle-counter intervals around
thread work include time spent preempted by an interrupt. The maxima for the
DWC2 and USBD layers are conservative boot-wide values and include USB
enumeration/control traffic. They are useful regression bounds, not
payload-only maxima. The sample also continuously calls `k_yield()`; this
experiment records hot-region cost rather than scheduler idle time or total
application CPU load.

The adapter instrumentation is optional and disabled in the normal sample.
`cpu_stats.conf` enables the ESP32-S3 cycle counter and emits a CSV record
after traffic becomes idle. DWC2 and USBD measurements used temporary local
Zephyr hooks at the ISR entry/exit, after the DWC2 event wait, and after the
USBD message-queue wait. Those hooks were removed after this run so the Zephyr
checkout and production firmware remain unmodified.

After removing the hooks, both the normal and `cpu_stats.conf` ESP32-S3 sample
images built successfully against a clean Zephyr checkout. The complete
Twister matrix also passed 22/22 executed configurations and 133/133 test
cases; three additional configurations were rejected by static platform
filters.

### CPU telemetry procedure and CSV v3

The optional telemetry image can be built against an unmodified Zephyr tree:

```sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/samples/zephyr/usb_bulk -d build/usb-bulk-cpu -- \
  -DEXTRA_CONF_FILE=/path/to/wirelink/samples/zephyr/usb_bulk/cpu_stats.conf
```

Run the same sequential `wirelink-bulk` host command used for the latency
profile. After the final TX completion has been quiet for 500 ms, the device
emits one logical CSV line. Its first field is the schema identifier
`wirelink_usb_cpu_v3`; every remaining pair of fields is a key followed by its
value. The line can therefore be parsed without depending on field order:

```python
import csv

fields = next(csv.reader([line]))
assert fields[0] == "wirelink_usb_cpu_v3"
assert len(fields) % 2 == 1
record = dict(zip(fields[1::2], fields[2::2], strict=True))
```

The fixed scalar keys are `cpu_hz`, `rx_claims`, `rx_completions`, `rx_bytes`,
`rx_pauses`, `tx_submissions`, `tx_completions`, and `errors`. The following
region prefixes add `_calls`, `_total_cycles`, and `_max_cycles` keys:

| Region prefix | Exact measured window | Counter scope |
| --- | --- | --- |
| `dwc2_isr` | Optional local hook around the DWC2 top half | Boot-wide |
| `dwc2_thread` | Optional local hook around one deferred DWC2 event | Boot-wide |
| `usbd_thread` | Optional local hook around one USBD event | Boot-wide |
| `adapter_active_service` | Adapter service entry to return when completion or rearm work exists | Boot-wide |
| `wl_poll` | `wl_poll()` entry to return; `k_uptime_get_32()` is excluded | Echo activity window |
| `rx_event_copy_release` | RX event metadata and payload copy through `wl_event_release()` return | Echo activity window |
| `wl_send_unreliable` | `wl_send_unreliable()` entry to return, including any nested sink call | Echo activity window |
| `wl_zephyr_usb_bulk_service` | Every full service call from entry to return, including inactive checks | Echo activity window |

The adapter also reports total and maximum cycles for
`adapter_rx_callback`, `adapter_tx_callback`, and `adapter_tx_sink`. Those
paths do not expose exact invocation counters, so v3
does not synthesize call counts for them.

An echo activity window opens when `wl_poll()` returns a reliable or
unreliable RX payload; that delivering poll call is included. It closes after
the corresponding TX completion is consumed by
`wl_zephyr_usb_bulk_service()`. If another response is submitted in that same
main-loop iteration, the window remains open. Consequently, the application
counters exclude boot/enumeration polling and the 500 ms quiet period, while
still including busy retries and the service call that closes each exchange.
Counters accumulate across multiple traffic bursts until reboot. Every
cumulative count, byte count, error count, and `_total_cycles` value in the CSV
is emitted as an unsigned 64-bit decimal value. Each `_max_cycles` value and
`cpu_hz` remain unsigned 32-bit values.

This width change is why the schema is v3 rather than v2. A typed consumer must
parse v3 cumulative fields as 64-bit values; it must not silently reuse a
32-bit v2 record model.

The ISR hooks and USB adapter deliberately retain their lock-free 32-bit
atomics and existing 32-bit public statistics API. The instrumented sample's
main loop snapshots them at most 100 ms apart while it is scheduled. For each
cumulative field it adds `(uint32_t)(current - previous)` to a private 64-bit
accumulator, so 32-bit wraparounds across a complete run do not truncate the
CSV. This modulo extension is exact provided an individual raw counter
advances by less than 2^32 between snapshots. At 240 MHz, 100 ms is only 24
million physical CPU cycles; USB byte and invocation counters have an even
wider margin. Do not call `wl_zephyr_usb_bulk_reset_stats()` during a telemetry
run because a reset is indistinguishable from a modulo wrap. The main-loop-only
`wl_poll`, copy/release, send, and full-service counters are updated directly
as 64-bit values and need no snapshot extension.

Maximum fields do not accumulate: the sample retains the largest 32-bit
maximum observed in any snapshot. An individual measured interval must remain
shorter than one full hardware cycle-counter period for its elapsed value to
be unambiguous; at 240 MHz that period is about 17.9 seconds. Reboot before a
formal run when a fresh maximum is required.

The regions deliberately describe execution intervals, not disjoint CPU
ownership. In particular:

- `adapter_tx_sink` is nested inside a successful
  `wl_send_unreliable` window;
- `adapter_active_service` is nested inside the corresponding full
  `wl_zephyr_usb_bulk_service` window;
- RX and TX class callbacks are nested inside `usbd_thread` when the optional
  USBD hook is present; and
- a DWC2 interrupt can preempt any thread or main-loop window, so that outer
  interval includes the preemption time.

Do not sum these totals as a CPU percentage. For a single region, compute mean
invocation time as `total_cycles / calls / cpu_hz`; divide by the exchange
count separately for per-exchange cost. With a clean Zephyr checkout the three
optional DWC2/USBD hook regions remain zero. The sample and adapter regions are
still fully populated and require no Zephyr source changes.

The counter updates themselves run just after their measured intervals and
perturb the instrumented firmware slightly. Use this image for attribution;
use the normal image for the retained host-latency numbers.

`CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS` remains disabled in `prj.conf`. In a
normal image, the sample's 64-bit extension state, cycle reads, completion
polling, and sample counter updates are all removed at preprocessing time, and
the adapter's cycle-counter callback remains null. The adapter's existing
operational atomics and null-callback checks are baseline transport work; the
claim here is zero incremental cost for the optional sample instrumentation,
not zero cost for every adapter statistic.

### 2026-08-31 clean-tree CSV v3 verification

The final v3 image was built from `e5121eb` against an unmodified Zephyr
`bd8c1538237` checkout and exercised on the same ESP32-S3-N8R2, native USB
port, and 240 MHz configuration. The host used Astrial `4ec4dc3`. Both the
normal and telemetry images built without warnings. The normal ELF contained
neither telemetry symbols nor the `wirelink_usb_cpu_v3` string, and its
Xtensa entries reserved 96 bytes for `wl_frame_encode()` and 48 bytes for
`wl_poll()`. The encoder previously reserved a 2,128-byte stack frame dominated
by a raw-frame temporary. Both translation units now have a 256-byte compiler
frame ceiling in the normal Zephyr build.

Three normal-image polling runs used a 120-byte payload, 1,000 warm-ups, and
10,000 measured exchanges:

| Run | p50 (us) | p95 (us) | p99 (us) | Max (us) | Mean (us) | Payload B/s |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 510.73 | 581.26 | 707.43 | 2,074.96 | 511.37 | 234,663.42 |
| 2 | 512.58 | 599.05 | 728.12 | 3,173.03 | 519.77 | 230,870.23 |
| 3 | 498.53 | 555.73 | 680.37 | 1,985.15 | 500.54 | 239,740.77 |

Run 1 is the median run by p50. All 33,000 exchanges, including warm-ups,
completed without a timeout, payload mismatch, or adapter error. Its 707.43 us
p99 remains inside a 1 ms round-trip interval; the 1.99--3.17 ms observed
maximum range still requires host-side jitter margin.

After enabling both stack gates, a final normal-image run with the same
parameters completed another 11,000 exchanges without failure: 478.33 us p50,
571.50 us p99, 1,393.91 us maximum, 487.31 us mean, and 246,249.32 payload
bytes/s. This is a post-gate regression check rather than a replacement for
the three-run profile.

One fresh v3 telemetry-image run then completed 11,000 total exchanges. It
reported 11,001 RX claims, 11,000 RX completions, 1,628,000 RX bytes, 336 RX
pauses, 11,000 TX submissions and completions, and zero adapter errors. The
optional clean-tree DWC2/USBD hook fields were zero, as expected. The host RTT
for this instrumented run was 497.55 us p50, 734.61 us p99, and 505.35 us mean;
these latency values are a verification aid and do not replace the normal
image results above.

| Measured region | Calls/exchange | Total cycles | Mean/call (us) | Time/exchange (us) | Max (us) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Adapter RX callback | -- | 28,837,067 | -- | 10.923 | 57.604 |
| Adapter TX callback | -- | 11,369,698 | -- | 4.307 | 8.896 |
| Adapter TX sink | -- | 67,161,318 | -- | 25.440 | 74.921 |
| Active adapter service | 1.970 | 89,777,898 | 17.262 | 34.007 | 74.513 |
| `wl_poll` | 35.782 | 279,740,792 | 2.961 | 105.962 | 184.875 |
| RX copy and release | 1.000 | 3,784,269 | 1.433 | 1.433 | 9.454 |
| `wl_send_unreliable` | 1.000 | 188,307,570 | 71.329 | 71.329 | 198.275 |
| Full USB service call | 35.782 | 173,921,735 | 1.841 | 65.879 | 65.808 |

The RX copy row's mean per call and time per exchange are identical because
there is one copy/release per exchange. The four non-overlapping
application-level measurement windows (`wl_poll`, copy/release, send, and full
service) cover
244.604 us per device exchange across the 1,000 warm-ups and 10,000 measured
exchanges. For scale, that is 48.4% of the host's 505.35 us measured-sample
mean, but the populations differ because the host excludes warm-ups. The ratio
is therefore an approximate cross-window comparison, not an exact coverage or
CPU-utilization measurement: interrupt preemption is included in an outer
interval, while loop bookkeeping and `k_yield()` are outside the named regions.

The clearest remaining device-CPU opportunity is scheduling. A sequential
exchange currently executes about 35 poll and full-service calls, so avoiding
idle consumer iterations can save more work than another small callback-path
micro-optimization. The proposed poll scheduling hint should therefore be
measured against these call counts and the normal-image p99, rather than only
against average callback duration.
