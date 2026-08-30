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
