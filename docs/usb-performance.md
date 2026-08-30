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
  --port /dev/ttyACM0 --payload 120 --warmup 1000 --iterations 10000
```

The VID/PIDs in the samples are development identifiers only and must be
replaced before product distribution.

## ESP32-S3 results

Physical measurements are pending a connected board. Record raw output and the
host/kernel/Zephyr revisions here; do not replace results with estimates.
