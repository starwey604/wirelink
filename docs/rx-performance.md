# RX SPSC performance standard

This benchmark compares Wirelink's two RX ring backends under one protocol
state machine. It is not a generic LwRB-versus-BipBuffer benchmark.

## Reference target

- Board: ESP32-S3-DevKitC-1, Zephyr target
  `esp32s3_devkitc/esp32s3/procpu`.
- CPU: fixed at 240 MHz with speed optimization enabled.
- UART fixture: connect GPIO17 (`UART1 TX`) directly to GPIO18 (`UART1 RX`),
  3 Mbaud, 8-N-1, no flow control.
- Disable logging, Wi-Fi, Bluetooth, power management, and dynamic frequency
  changes in measured images. Use UART0 only for CSV results.
- RX usable capacity is 4096 bytes. The BipBuffer backend receives 4096
  physical bytes; LwRB receives 4097 because its full/empty convention reserves
  one byte. Both use the same alignment and a 2084-byte fallback buffer.
- Payload sizes are 16, 64, 256, 1024, and 2048 bytes. Payload contents come
  from the deterministic generator in the benchmark image. Frames use COBS and
  CRC32C.

## Ingress modes

`IRQ` drains the UART FIFO into a 64-byte ISR-local chunk and calls
`wl_feed_bytes()`. The ISR performs no framing, CRC, ACK, or application work.

`DMA` hands a bounded contiguous span returned by `wl_rx_reserve()` to the
ESP32-S3 UART async/GDMA driver. A completed DMA buffer is published with
`wl_rx_commit()`; decoding remains in the main loop. Because the producer API
allows one outstanding reservation, the loopback sender waits for each
64-byte-or-shorter span to be committed before the main loop re-arms GDMA. It
does not queue a second driver-owned span or hide that re-arm cost.

`USB` accepts CDC ACM OUT data from a PC with DWC2 buffer DMA enabled. The USB
stack owns an endpoint buffer, so CDC ingress still calls `wl_feed_bytes()` and
contains one endpoint-to-ring copy. USB results are end-to-end adapter evidence,
not the deciding ring microbenchmark.

## Runs and output

- Primitive reserve/commit and feed microbenchmarks: 10,000 warm-up operations
  followed by 100,000 measured operations for 1, 8, 32, 64, and 256-byte
  producer chunks. The `reserve_commit` cycle count excludes the simulated
  producer's memory writes; `feed` includes Wirelink's copy. Consumer cleanup
  occurs outside the measured interval so the producer cursor still exercises
  physical wrap.
- UART frame benchmark: 1,000 warm-up frames followed by 10,000 measured frames
  per payload size. Flash and run each image for five independent reset runs.
- Capture the build revision, backend, ingress mode, payload size, frame count,
  CPU frequency, `.text`, `.data`, `.bss`, usable/physical RX size, accepted and
  dropped bytes, RX overflow count, producer calls/cycles, frames per second,
  and latency median/p95/max.
- The firmware emits CSV beginning with `wirelink_rx_bench_v1`. Preserve the
  raw serial log for every run; do not average different firmware images.
  Its `meta` row records the image's cycle frequency and RX buffer sizes;
  obtain section sizes from the matching Zephyr build report.
  ESP32-S3 measurements use its hardware CPU cycle counter, not the Zephyr
  system-tick counter. `run_cycles` is the sum of the measured frame latencies,
  which avoids a 32-bit hardware-counter rollover during long profiles.

Correctness is a gate: no corrupted delivery, invalid lease, unexpected drop,
or failed SPSC test is permitted. Among correct backends, choose the one with
the lower UART producer cycles per byte. If the difference is within measurement
noise, compare UART throughput, p95 latency, then RAM and flash. USB numbers are
reported separately and never override the UART decision.

## Build-only matrix

From the Zephyr workspace, build without flashing:

```sh
west twister -T /path/to/wirelink/benchmarks/zephyr/rx_backend \
  -p esp32s3_devkitc/esp32s3/procpu --build-only
```

The matrix builds both backends with IRQ and DMA ingress, plus both USB CDC
images. Actual UART and USB measurements require the physical board and are not
run by Twister.
