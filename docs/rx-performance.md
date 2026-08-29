# RX SPSC buffer selection and performance record

Wirelink now has one RX byte-buffer implementation: its atomic SPSC
BipBuffer (`src/rx_ring_bipbuf.c`). This document records the comparison that
led to that decision and the procedure retained for future regression checks.

## Selection

The RX buffer is on the hot path between one ISR or DMA-completion producer and
one main-loop consumer. It must provide an acquire/release SPSC hand-off, a
continuous producer reservation, a continuous consumer view for in-place COBS
decoding, and must retain borrowed frame bytes until `wl_event_release()`.

The custom BipBuffer is the fixed implementation because it meets those
requirements while using all caller-provided storage and giving the producer
and consumer contiguous spans without a wrapper or an extra capacity byte. Its
cursor ownership and C11 atomics are explicit: the producer release-publishes
the write cursor after writing data; the consumer acquire-loads it, and uses the
matching ordering when returning space. Initialization rejects targets where
the required atomics are not lock-free.

MaJerle/LwRB v3.3.0 was evaluated as an alternative. It also supplied SPSC
operations and continuous spans, but required 4097 physical bytes for a 4096
byte usable ring and added a vendored dependency and a selectable-backend
surface. On the measured UART producer paths it was materially slower. The
backend switch, its Kconfig/CMake choices, test variants, and vendored source
were therefore removed. This is a deliberate fixed implementation, not a
promise of runtime or build-time RX backend interchangeability.

The decision concerns the byte buffer only. A frame that crosses the physical
end still takes Wirelink's documented fallback-copy path; a contiguous COBS
frame is decoded in place and its payload remains borrowed until release.

## ESP32-S3 UART comparison — 2026-08-16

This is the decision dataset, collected before LwRB was removed at source
revision `880fa9e` (`bench: use esp32 cycle counter`). Each of the four images
was flashed and measured once. It is sufficient to distinguish the observed
large producer-cost difference, but it is not a five-reset statistical study;
repeat the procedure below after a material RX-path change.

Fixture and image settings:

- ESP32-S3-DevKitC-1, Zephyr target
  `esp32s3_devkitc/esp32s3/procpu`, CPU fixed at 240 MHz.
- UART1 loopback: GPIO17 (TX) wired directly to GPIO18 (RX), 3 Mbaud, 8-N-1,
  no flow control. UART0 carried the serial CSV output.
- Logging, Wi-Fi, Bluetooth, power management, and dynamic frequency changes
  were disabled in the measured image.
- A 4096-byte usable ring and 2084-byte fallback buffer were used. LwRB's
  historical comparison image received 4097 physical bytes because it reserves
  one byte to distinguish full from empty.
- The UART profile uses 1,000 warm-up frames then 10,000 measured frames. The
  CPU hardware cycle counter measures producer calls and end-to-end latency.

At the largest tested payload (2048 bytes), all four images completed with
zero dropped bytes and no terminal ingress error:

| Buffer | Ingress | Producer cycles | Accepted bytes | Cycles/byte | Median latency | p95 latency | Max latency |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| BipBuffer | UART IRQ | 603,811,292 | 20,830,312 | 28.99 | 1,777,214 | 1,778,410 | 1,779,775 |
| LwRB (historical) | UART IRQ | 1,075,199,097 | 20,830,312 | 51.62 | 1,853,744 | 1,854,970 | 1,857,033 |
| BipBuffer | UART DMA | 27,615,292 | 20,830,312 | 1.326 | 1,880,331 | 1,884,017 | 1,887,307 |
| LwRB (historical) | UART DMA | 61,054,189 | 20,830,312 | 2.931 | 1,925,594 | 1,929,680 | 1,933,529 |

Relative to the historical LwRB image, BipBuffer used 43.8% fewer producer
cycles per byte in IRQ ingress and 54.8% fewer in the current DMA ingress.
The DMA numbers do **not** represent the current direct-DMA architecture. That
historical benchmark waited to commit and re-arm each 64-byte-or-shorter
transfer. Wirelink now supports two ordered direct claims, and the Zephyr
adapter can map them to ping-pong buffers without changing the SPSC buffer
choice.

The primitive profiles below used 10,000 warm-up operations and 100,000
measured operations. `reserve/commit` omits the simulated producer's writes;
`feed` includes Wirelink's copy. Values are measured cycles, with the actual
number of reserve calls in parentheses.

| Chunk | BipBuffer reserve/commit | LwRB reserve/commit (historical) | BipBuffer feed | LwRB feed (historical) |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 15,454,618 (100,000) | 23,799,976 (100,000) | 21,777,088 | 30,133,037 |
| 8 | 15,450,832 (100,000) | 23,842,970 (100,171) | 22,357,816 | 33,148,576 |
| 32 | 15,452,501 (100,000) | 23,980,894 (100,757) | 23,750,470 | 35,002,967 |
| 64 | 15,690,766 (101,562) | 24,163,832 (101,538) | 26,009,188 | 37,879,143 |
| 256 | 16,413,227 (106,250) | 25,259,373 (106,224) | 38,779,578 | 54,673,654 |

USB CDC was intentionally not measured in this round and is not a decision
input. It remains a build-only benchmark ingress fixture; its endpoint buffer
would require a CDC-to-ring copy, so even future USB results must not override
the UART SPSC-buffer decision.

## ESP32-S3 direct-DMA adapter validation — 2026-08-30

This round validates the Zephyr async-UART adapter added on top of the direct
DMA claim API. It is a correctness smoke test rather than a replacement for
the five-reset performance dataset above.

Fixture and image settings:

- ESP32-S3-N8R2 revision 0.2, Zephyr
  `v4.4.0-11610-gbd8c15382376`, 240 MHz.
- UART1 at 3 Mbaud with GPIO17 (TX) wired to GPIO18 (RX); UART0 on
  `/dev/ttyACM0` carried benchmark output.
- 4096-byte RX ring, a 4096-byte maximum direct claim, and a 200 us finite RX
  timeout. Finite-timeout mode deliberately uses one buffer, releases a short
  claim in consumer context, and then re-arms RX.
- One warm-up frame and ten measured frames per payload. Producer cycles cover
  the cache-completion hook, direct-prefix publication, and adapter accounting
  performed by `UART_RX_RDY`.

The final sequential run completed through 1024 bytes without corruption,
drops, RX counter failures, or adapter errors. Raw result rows were:

| Payload | Frames | Producer cycles | Accepted bytes | Dropped | Median latency | p95/p99/max latency |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 10 | 1,976 | 440 | 0 | 66,411 | 67,354 |
| 20 | 10 | 1,975 | 480 | 0 | 70,841 | 80,935 |
| 64 | 10 | 1,975 | 920 | 0 | 118,894 | 121,746 |
| 120 | 10 | 1,975 | 1,480 | 0 | 170,703 | 177,179 |
| 256 | 10 | 3,433 | 2,840 | 0 | 306,586 | 315,105 |
| 1024 | 10 | 3,920 | 10,550 | 0 | 1,074,242 | 1,075,749 |

The median figures are 0.277 ms, 0.295 ms, 0.495 ms, 0.711 ms, 1.277 ms, and
4.476 ms respectively at 240 MHz. They include polled transmission on the
same UART and therefore are fixture end-to-end numbers, not isolated adapter
cost. The producer-side adapter work was about 197.5 cycles per `UART_RX_RDY`
call through 120-byte payloads, 343.3 cycles for 256 bytes, and 392 cycles for
1024 bytes in this short run.

The same-UART loopback is not a valid sustained-stress fixture for this driver.
After the successful 1024-byte row, the Zephyr ESP32 UART path stopped making
progress while the benchmark was inside `uart_poll_out()`. An isolated run
starting directly at the 2048-byte profile reproduced the same behavior before
its first result row. Because the main thread never reached `wait_for_frame()`,
there was no Wirelink timeout, malformed-frame counter, drop, or adapter error
to report. Earlier stress attempts likewise progressed for different numbers
of repeated disable/re-arm cycles before stopping, including 1,000 successful
16-byte frames and 1,000 successful 20-byte frames with no drops or adapter
errors.

Consequently this round establishes on-board direct-DMA correctness for
16–1024-byte frames, but leaves 2048-byte and sustained-throughput validation
open. Those gates require an independent UART transmitter so TX polling and RX
DMA do not share the ESP32 UART driver instance. USB CDC remained intentionally
out of scope. The benchmark's `BENCH_PAYLOAD_START_INDEX` build override was
added so an external-fixture run can select the 2048-byte profile directly.

## Regression procedure

Correctness is a gate: no corrupted delivery, invalid lease, unexpected drop,
or failed SPSC test is permitted. For a future hardware comparison, use five
independent reset runs per image, preserve every raw CSV log, and compare the
same firmware revision, payload set (16, 20, 64, 120, 256, 1024, 2048),
frequency, buffer sizes, and UART fixture. The 20-byte and 120-byte profiles
represent one joint command and a six-joint aggregate command respectively.
Compare UART producer cycles per byte first; if a future change is within
measurement noise, then compare throughput, p95/p99 latency, and image/RAM
size.

The ESP32-S3 benchmark remains build-only in Twister and can be compiled with:

```sh
cd ~/zephyrproject/zephyr
../.venv/bin/west twister -T /path/to/wirelink/benchmarks/zephyr/rx_backend \
  -p esp32s3_devkitc/esp32s3/procpu --build-only
```

Physical UART measurement requires the loopback fixture and is never performed
by Twister.
